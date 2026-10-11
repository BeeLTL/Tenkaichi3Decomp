// Benchmark of the browser build on the game's attract-demo battle (BT3_DEMO): the same scene in every run (the
// game's random seed is fixed, plat_sys.c; no input). Drives Chrome / Edge through the DevTools protocol with a
// fresh throwaway profile, reads the per-frame lines of BT3_FRAME_LOG (gs_core.c) and the fight trace of BT3_TRACE
// (headless.c), and optionally records Chrome's CPU profile over the measured frames.
//
//   node port/tests/bench/bench.js --label <name> [--browser <exe>] [--url http://127.0.0.1:8080/index.html]
//        [--from 600] [--to 1500] [--profile] [--headless] [--env NAME=value,...]
//
// The page and the data must be served (port/web/devserver.py with the gamedata mount). Frames [from, to) are
// measured; frames before `from` are the warm-up (loading, first textures and pipelines). Writes
// port/build/bench/<label>.json and prints a summary.
'use strict';
const { spawn } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const crypto = require('crypto');

const args = Object.fromEntries(process.argv.slice(2).reduce((acc, a, i, all) => {
  if (a.startsWith('--')) acc.push([a.slice(2), all[i + 1] && !all[i + 1].startsWith('--') ? all[i + 1] : true]);
  return acc;
}, []));
const label = args.label || 'run';
const browser = args.browser || 'C:/Program Files/Google/Chrome/Application/chrome.exe';
const from = Number(args.from || 600), to = Number(args.to || 1500);
const port = 9300 + Math.floor(Math.random() * 500);
const env = ['BT3_DEMO=1', 'BT3_FRAME_LOG=1', 'BT3_TRACE=60'].concat(args.env && args.env !== true ? args.env.split(',') : []);
const url = `${args.url || 'http://127.0.0.1:8080/index.html'}?autostart=1&env=${env.join(',')}`;
const profileDir = fs.mkdtempSync(path.join(os.tmpdir(), 'bt3-bench-'));
const outDir = path.join(__dirname, '../../build/bench');
fs.mkdirSync(outDir, { recursive: true });

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const chrome = spawn(browser, [`--remote-debugging-port=${port}`, `--user-data-dir=${profileDir}`, '--no-first-run',
  '--no-default-browser-check', '--disable-extensions', '--disable-sync', '--autoplay-policy=no-user-gesture-required',
  '--disable-background-timer-throttling', '--disable-renderer-backgrounding', '--window-size=1280,1150',
  ...(args.headless ? ['--headless=new'] : []), 'about:blank'], { stdio: 'ignore' });

function stats(xs) {
  const s = [...xs].sort((a, b) => a - b), n = s.length;
  const q = (p) => s[Math.min(n - 1, Math.ceil(p * n) - 1)];
  return { n, mean: s.reduce((a, b) => a + b, 0) / n, p50: q(0.5), p95: q(0.95), p99: q(0.99), max: s[n - 1],
           over: s.filter((v) => v > 33366.7).length };
}

// Self time per function from a CDP profile (sampled), grouped in categories.
function summarize(profile) {
  const self = new Map(), byId = new Map(profile.nodes.map((n) => [n.id, n]));
  profile.samples.forEach((id, i) => {
    const n = byId.get(id), dt = profile.timeDeltas[i] || 0;
    const key = (n.callFrame.functionName || '(anonymous)') + (n.callFrame.url.endsWith('.wasm') ? '' : ` [${path.basename(n.callFrame.url) || 'native'}]`);
    self.set(key, (self.get(key) || 0) + dt);
  });
  const total = [...self.values()].reduce((a, b) => a + b, 0);
  const cat = (k) =>
    /^\((idle|program|garbage collector)\)/.test(k) ? k.replace(/ \[.*/, '') :
    /fpcast-emu|byn\$fpcast/.test(k) ? 'function-pointer cast thunks' :
    /^(__(add|sub|mul|div|neg)sf3|__(lt|le|gt|ge|eq|ne|unord)sf2|__fix\w*sf\w*|__float\w*sf|Port_Fast\w+|RefVu0_\w+|Sf_\w+|fpu_add|mul_core\w*|add_core\w*|divide)\b/.test(k) ? 'PS2 float routines' :
    /^(Port_(sinf|cosf|tanf|atan2f|atanf|asinf|acosf|sqrtf|powf|floorf|ceilf|fmodf)|__ieee754|__kernel|Port_scalbnf)/.test(k) ? 'PS2 maths library' :
    /\[bt3\.js\]/.test(k) && /^_?(gl|emscripten_gl|webgl)/i.test(k) ? 'WebGL calls (JS glue)' :
    /\[bt3\.js\]/.test(k) ? 'other JS glue (bt3.js)' :
    /\[(native|)\]$/.test(k) ? 'browser native' :
    /^(Gs|gs_|GsGpu|GsVu1|GsDraw|Port_Gs|vif|run_chain|unpack|hle_|vu1_|draw_|record_|tex_|pipe|target_|gl_|apply_|set_params|present)/.test(k) ? 'renderer (GS front end, VU1, back end)' :
    /^(memcpy|memset|memmove|malloc|free|dlmalloc|dlfree|strlen|strcmp|sprintf|printf|vfprintf|fwrite|__stdio)/.test(k) ? 'C library' :
    'game code and the rest';
  const cats = new Map();
  for (const [k, v] of self) cats.set(cat(k), (cats.get(cat(k)) || 0) + v);
  const top = [...self].sort((a, b) => b[1] - a[1]).slice(0, 40).map(([k, v]) => ({ fn: k, ms: v / 1000, pct: 100 * v / total }));
  return { totalMs: total / 1000, categories: [...cats].sort((a, b) => b[1] - a[1]).map(([k, v]) => ({ cat: k, pct: 100 * v / total })), top };
}

(async () => {
  let ver;
  for (let i = 0; i < 100 && !ver; i++) {
    try { ver = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json(); } catch (e) { await sleep(200); }
  }
  if (!ver) throw new Error('the browser did not open its debugging port');
  const target = (await (await fetch(`http://127.0.0.1:${port}/json/list`)).json()).find((t) => t.type === 'page');
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((r) => ws.addEventListener('open', r, { once: true }));
  let id = 0;
  const waiting = new Map();
  const send = (method, params = {}) => new Promise((res, rej) => {
    const i = ++id;
    waiting.set(i, { res, rej });
    ws.send(JSON.stringify({ id: i, method, params }));
  });
  const frames = [], trace = [], log = [];
  let profiling = false, done, stopped = null;
  const finished = new Promise((r) => { done = r; });
  ws.addEventListener('message', (ev) => {
    const m = JSON.parse(ev.data);
    if (m.id && waiting.has(m.id)) {
      const w = waiting.get(m.id);
      waiting.delete(m.id);
      m.error ? w.rej(new Error(m.error.message)) : w.res(m.result);
    }
  });
  // The page queues its log lines with their time (index.html, window.bt3Out); they are collected a few times a
  // second, so that the measurement does not go through the console inside the measured frames.
  async function handle(ts, line) {
    if (line.startsWith('frame ')) {
      const f = line.split(' ').slice(1).map(Number);  // frame vblank work game list tex pipe end (microseconds)
      f.push(ts);                                       // + when the page logged it (ms, performance.now)
      frames.push(f);
      if (f[0] >= from && !profiling && args.profile) {
        profiling = true;
        await send('Profiler.setSamplingInterval', { interval: 200 });
        await send('Profiler.start');
      }
      if (f[0] >= to) done();
      return;
    }
    if (line.startsWith('vb ')) trace.push(line);
    else log.push(line);
    if (line.startsWith('STOPPED')) { stopped = line; done(); }
  }
  await send('Runtime.enable');
  await send('Profiler.enable');
  await send('Page.enable');
  const t0 = Date.now();
  await send('Page.navigate', { url });
  const timeout = setTimeout(() => { stopped = stopped || 'timeout (no frame reached ' + to + ')'; done(); }, Number(args.timeout || 600) * 1000);
  let over = false;
  finished.then(() => { over = true; });
  while (!over) {
    await sleep(250);
    let r;
    try {
      r = await send('Runtime.evaluate', { expression: 'JSON.stringify(window.bt3Out ? window.bt3Out.splice(0) : [])', returnByValue: true });
    } catch (e) { continue; }  // (the page is being replaced)
    for (const [ts, line] of JSON.parse(r.result.value || '[]')) {
      if (!over) await handle(ts, line);
    }
  }
  await finished;
  clearTimeout(timeout);
  const profile = profiling ? (await send('Profiler.stop')).profile : null;
  const win = frames.filter((f) => f[0] >= from && f[0] < to);
  const result = {
    label, url, browser: ver.Browser, when: new Date().toISOString(), seconds: (Date.now() - t0) / 1000, stopped,
    gpu: (log.find((l) => l.startsWith('gpu: ')) || '').slice(5), window: [from, to],
    work: win.length ? stats(win.map((f) => f[2])) : null, game: win.length ? stats(win.map((f) => f[3])) : null,
    list: win.length ? stats(win.map((f) => f[4])) : null,
    submit: win.length ? stats(win.map((f) => f[7])) : null,
    // wall clock over the window: real frames per second, and the time between frames
    wallSeconds: win.length > 1 ? (win[win.length - 1][8] - win[0][8]) / 1000 : null,
    fps: win.length > 1 ? (win.length - 1) / ((win[win.length - 1][8] - win[0][8]) / 1000) : null,
    interval: win.length > 1 ? stats(win.slice(1).map((r, i) => (r[8] - win[i][8]) * 1000)) : null,
    vblanksPerFrame: win.length > 1 ? (win[win.length - 1][1] - win[0][1]) / (win.length - 1) : null,
    traceLines: trace.length, traceHash: crypto.createHash('sha1').update(trace.join('\n')).digest('hex').slice(0, 16),
    glErrors: log.filter((l) => /gl: frame error/.test(l)).length,
    profile: profile ? summarize(profile) : null,
  };
  fs.writeFileSync(path.join(outDir, `${label}.json`), JSON.stringify(result, null, 1));
  fs.writeFileSync(path.join(outDir, `${label}.trace.txt`), trace.join('\n') + '\n');
  if (profile) fs.writeFileSync(path.join(outDir, `${label}.cpuprofile`), JSON.stringify(profile));
  fs.writeFileSync(path.join(outDir, `${label}.frames.csv`), 'frame,vblank,work_us,game_us,list_us,tex_us,pipe_us,submit_us,t_ms\n' +
                   frames.map((r) => r.join(',')).join('\n') + '\n');
  fs.writeFileSync(path.join(outDir, `${label}.log.txt`), log.join('\n') + '\n');
  const f = (s) => s ? `mean ${(s.mean / 1000).toFixed(2)}  p50 ${(s.p50 / 1000).toFixed(2)}  p95 ${(s.p95 / 1000).toFixed(2)}  p99 ${(s.p99 / 1000).toFixed(2)}  max ${(s.max / 1000).toFixed(2)} ms  over ${s.over}/${s.n}` : '-';
  console.log(`${label}: ${result.browser}; gpu ${result.gpu}; frames ${from}..${to}${stopped ? '; STOPPED: ' + stopped : ''}`);
  console.log(`  work   ${f(result.work)}\n  game   ${f(result.game)}\n  list   ${f(result.list)}\n  submit ${f(result.submit)}`);
  console.log(`  wall   ${result.fps ? result.fps.toFixed(1) : '-'} frames/s over ${result.wallSeconds ? result.wallSeconds.toFixed(1) : '-'} s; ` +
              `${result.vblanksPerFrame ? result.vblanksPerFrame.toFixed(2) : '-'} vblanks per frame; interval ${f(result.interval)}`);
  console.log(`  trace: ${result.traceLines} lines, hash ${result.traceHash}; GL errors logged: ${result.glErrors}`);
  if (result.profile) {
    console.log('  profile (self time):');
    for (const c of result.profile.categories) console.log(`    ${c.pct.toFixed(1).padStart(5)}%  ${c.cat}`);
    console.log('  top functions:');
    for (const t of result.profile.top.slice(0, 25)) console.log(`    ${t.pct.toFixed(1).padStart(5)}%  ${t.fn}`);
  }
  ws.close();
  chrome.kill();
  await sleep(500);
  try { fs.rmSync(profileDir, { recursive: true, force: true }); } catch (e) { /* the browser may still hold files */ }
  process.exit(stopped ? 1 : 0);
})().catch((e) => { console.error(e); chrome.kill(); process.exit(2); });
