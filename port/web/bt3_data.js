// The game's data for the browser build, read on demand (port/src/web/web_fs.c calls size() and read()).
//
// The data folder (gamedata/, made from the user's own disc image) stays on a server the user runs for
// themselves (port/web/devserver.py, 127.0.0.1); nothing of it is part of the build. Files are read in blocks with
// HTTP Range requests and kept in an in-memory block cache (least recently used block dropped first). Sizes come
// from <base>manifest.json (names and sizes only) when it exists, else from a HEAD request per file. No tool in the
// repository makes that manifest yet: today the sizes come from HEAD requests.
'use strict';

class Bt3Data {
  constructor(base, { blockSize = 256 * 1024, cacheBytes = 256 * 1024 * 1024, log = console.log } = {}) {
    this.base = base.endsWith('/') ? base : base + '/';
    this.blockSize = blockSize;
    this.cacheBytes = cacheBytes;
    this.log = log;
    this.blocks = new Map();   // "rel#index" -> Uint8Array, in order of use (oldest first)
    this.pending = new Map();  // "rel#index" -> Promise of the block being fetched
    this.cached = 0;
    this.sizes = new Map();
    this.manifest = null;
    this.stats = { requests: 0, bytes: 0, hits: 0, misses: 0, errors: 0, files: 0 };
    this.module = null;        // set by the page: the Emscripten module (for HEAPU8)
  }

  url(rel) {
    return this.base + rel.split('/').map(encodeURIComponent).join('/');
  }

  // Loads the manifest if the server has one. Returns the number of files it lists (0: none, sizes by HEAD).
  async init() {
    try {
      const r = await fetch(this.base + 'manifest.json', { cache: 'no-cache' });
      if (r.ok) {
        this.manifest = await r.json();
        return Object.keys(this.manifest).length;
      }
    } catch (e) {
      this.log(`data: no manifest (${e.message})`);
    }
    return 0;
  }

  // A file's size in bytes; -1 if the server has no such file; -2 if the server failed.
  async size(rel) {
    if (this.sizes.has(rel)) return this.sizes.get(rel);
    let size;
    if (this.manifest) {
      size = Object.prototype.hasOwnProperty.call(this.manifest, rel) ? this.manifest[rel] : -1;
    } else {
      let r;
      try {
        r = await fetch(this.url(rel), { method: 'HEAD' });
      } catch (e) {
        this.stats.errors++;
        this.log(`data: HEAD ${rel}: ${e.message}`);
        return -2;
      }
      this.stats.requests++;
      if (r.status === 404) {
        size = -1;
      } else if (!r.ok) {
        this.stats.errors++;
        this.log(`data: HEAD ${rel}: HTTP ${r.status}`);
        return -2;
      } else {
        size = Number(r.headers.get('Content-Length'));
        if (!Number.isFinite(size)) {
          this.stats.errors++;
          this.log(`data: HEAD ${rel}: no Content-Length`);
          return -2;
        }
      }
    }
    if (size >= 0) this.stats.files++;
    this.sizes.set(rel, size);
    return size;
  }

  // Reads n bytes at off into the module's memory at dst. Returns the bytes read (fewer at the end), -1 on error.
  async read(rel, off, dst, n) {
    const size = await this.size(rel);
    if (size < 0) return -1;
    if (off >= size || n <= 0) return 0;
    const end = Math.min(size, off + n);
    const bs = this.blockSize;
    let pos = off;
    try {
      for (let bi = Math.floor(off / bs); pos < end; bi++) {
        const block = await this.block(rel, bi, size);
        const from = pos - bi * bs, take = Math.min(block.length - from, end - pos);
        // (HEAPU8 is read after the await: memory may have grown meanwhile)
        this.module.HEAPU8.set(block.subarray(from, from + take), dst + (pos - off));
        pos += take;
      }
    } catch (e) {
      this.stats.errors++;
      this.log(`data: ${rel} @${off}+${n}: ${e.message}`);
      return -1;
    }
    return pos - off;
  }

  async block(rel, bi, size) {
    const key = rel + '#' + bi;
    const have = this.blocks.get(key);
    if (have) {
      this.stats.hits++;
      this.blocks.delete(key);  // most recently used last
      this.blocks.set(key, have);
      return have;
    }
    if (this.pending.has(key)) return this.pending.get(key);
    this.stats.misses++;
    const p = this.fetchBlock(rel, bi, size).finally(() => this.pending.delete(key));
    this.pending.set(key, p);
    return p;
  }

  async fetchBlock(rel, bi, size) {
    const start = bi * this.blockSize, last = Math.min(size, start + this.blockSize) - 1;
    const r = await fetch(this.url(rel), { headers: { Range: `bytes=${start}-${last}` } });
    this.stats.requests++;
    let data;
    if (r.status === 206) {
      const cr = /^bytes (\d+)-(\d+)\/(\d+|\*)$/.exec(r.headers.get('Content-Range') || '');
      if (!cr || Number(cr[1]) !== start || Number(cr[2]) !== last) {
        throw new Error(`asked for bytes ${start}-${last}, the server sent "${r.headers.get('Content-Range')}"`);
      }
      data = new Uint8Array(await r.arrayBuffer());
    } else if (r.status === 200) {
      // a server without Range support sends the whole file: keep the block that was asked for
      const all = new Uint8Array(await r.arrayBuffer());
      if (all.length !== size) throw new Error(`the server sent ${all.length} bytes, the file has ${size}`);
      data = all.slice(start, last + 1);
    } else {
      throw new Error(`HTTP ${r.status}`);
    }
    if (data.length !== last - start + 1) {
      throw new Error(`short block: ${data.length} of ${last - start + 1} bytes`);
    }
    this.stats.bytes += data.length;
    this.blocks.set(rel + '#' + bi, data);
    this.cached += data.length;
    for (const [k, v] of this.blocks) {  // oldest first
      if (this.cached <= this.cacheBytes) break;
      this.blocks.delete(k);
      this.cached -= v.length;
    }
    return data;
  }

  // Fetches a whole file (used before start-up for the files read before main can wait).
  async whole(rel) {
    const r = await fetch(this.url(rel));
    this.stats.requests++;
    if (!r.ok) throw new Error(`${rel}: HTTP ${r.status} from ${this.url(rel)}`);
    const data = new Uint8Array(await r.arrayBuffer());
    this.stats.bytes += data.length;
    return data;
  }
}

if (typeof module !== 'undefined') module.exports = { Bt3Data };
