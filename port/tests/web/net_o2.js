// Online play in the browser, test O2 (docs/BROWSER_ONLINE_PLAN.md, P2): two copies of the page, in two separate
// headless Chrome instances on this machine, connected by a WebRTC data channel (port/web/net_web.js), play a match
// through upstream's net.c (port/src/web/net_web.c below it). The test does the signalling (offer / answer) itself.
// Each copy writes its state checksum every blank (BT3_HASH, in the page's file system); then:
//   - host and joiner agree on the WHOLE state at every blank both completed (same build: comparable);
//   - neither reports a desync ("no longer agree");
//   - with 10% of the packets dropped and 100 ms one-way latency (upstream's BT3_NET_LOSS / BT3_NET_LATENCY) too;
//   - the attract demo's fight played over the connection has the same fight values, blank by blank, as the same
//     fight in one page with no network.
// Rollback is not in the browser build (state.c cannot save WebAssembly's call stack): net.c waits for the other
// side (input delay), as upstream's 32-bit Linux build does.
//   node port/tests/web/net_o2.js [seconds per scenario, default 75]
// Needs the dev server on 127.0.0.1:8080 with the game data (port/web/devserver.py --mount gamedata=...).
'use strict';
const path = require('path'), fs = require('fs'), os = require('os');
const { execFileSync } = require('child_process');
const { launch, evaluate, sleep } = require('../lib/cdp.js');

const BASE = 'http://127.0.0.1:8080/index.html?autostart=1';
const SECONDS = Number(process.argv[2] || 75);
const MIN_BLANKS = 1500, MIN_FIGHT = 1000;
const ZERO = '0000000000000000';
const ARGS = ['--disable-features=WebRtcHideLocalIpsWithMdns'];  // (headless: local candidates as plain addresses)
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failed++; };

// the input table: upstream's format (36 bytes per blank), made by port/tests/native/net_script.py
const scriptFile = path.join(os.tmpdir(), 'bt3_net_script.bin');
execFileSync(process.platform === 'win32' ? 'python' : 'python3', [path.join(__dirname, '../native/net_script.py'), scriptFile, '6000']);
const SCRIPT_B64 = fs.readFileSync(scriptFile).toString('base64');

async function page(env, script) {
  const b = await launch({ headless: true, extraArgs: ARGS });
  const ev = (e) => evaluate(b.send, e);
  await b.send('Page.enable');
  await b.send('Page.addScriptToEvaluateOnNewDocument', { source: `globalThis.bt3TestPreRun = (m) => {
    m.FS.writeFile('/net_script.bin', ${script ? `Uint8Array.from(atob('${SCRIPT_B64}'), (c) => c.charCodeAt(0))` : 'new Uint8Array(0)'});
  };` });
  await b.send('Page.navigate', { url: `${BASE}&env=${encodeURIComponent(env.join(','))}` });
  for (let i = 0; i < 100 && !(await ev(`!!globalThis.bt3Net`).catch(() => false)); i++) await sleep(100);
  return { b, ev };
}

// completed lines of the checksum log, by blank
async function hashes(p) {
  const text = await p.ev(`(() => { try { return bt3Module.FS.readFile('/net.hash', { encoding: 'utf8' }); } catch (e) { return ''; } })()`);
  const lines = text.endsWith('\n') ? text.split('\n') : text.split('\n').slice(0, -1);
  const d = new Map();
  for (const l of lines) if (l.trim()) d.set(Number(l.split(' ')[0]), l.split(' '));
  return d;
}

async function scenario(name, extra, { script = true } = {}) {
  console.log(`--- ${name}`);
  const common = ['BT3_NET_SCRIPT=/net_script.bin', 'BT3_HASH=/net.hash', 'BT3_SOUND_TICKS=1', ...extra];
  // The two settings have the same length on purpose. The game keeps the addresses of blocks the port gives it (file
  // handles) in its memory; in this build they come from the C library's allocator (as in upstream's 32-bit build;
  // the 64-bit builds have a region of their own, plat_mem.c PORT_LOW_REGION), whose layout follows the length of the
  // environment's text: with "BT3_NET_HOST=1" against "BT3_NET_JOIN=127.0.0.1:1" those addresses differ by 16
  // and nothing else does (5 bytes, found with BT3_HASH_AT). The game never computes with them. (The host's port and
  // the joiner's address are not used by the browser's transport: one peer, the page's data channel.)
  const host = await page(['BT3_NET_HOST=999999999', ...common], script);
  const join = await page(['BT3_NET_JOIN=9.9.9.9:9', ...common], script);
  const t0 = Date.now();
  const offer = await host.ev(`bt3Net.offer()`);
  const answer = await join.ev(`bt3Net.answer(${JSON.stringify(offer)})`);
  await host.ev(`bt3Net.accept(${JSON.stringify(answer)})`);
  for (let i = 0; i < 100 && !(await host.ev(`bt3Net.open`) && await join.ev(`bt3Net.open`)); i++) await sleep(100);
  check(await host.ev(`bt3Net.open`) && await join.ev(`bt3Net.open`), `${name}: data channel open after ${Date.now() - t0} ms`);
  await sleep(SECONDS * 1000);
  const [h, j] = [await hashes(host), await hashes(join)];
  const logs = [await host.ev(`document.getElementById('log').textContent`), await join.ev(`document.getElementById('log').textContent`)];
  const stats = [await host.ev(`JSON.stringify(bt3Net.stats)`), await join.ev(`JSON.stringify(bt3Net.stats)`)];
  await host.b.close(); await join.b.close();
  const both = [...h.keys()].filter((t) => j.has(t)).sort((a, b) => a - b);
  const diff = both.filter((t) => h.get(t).slice(1).join(' ') !== j.get(t).slice(1).join(' '));
  const fight = both.filter((t) => h.get(t)[2] !== ZERO).length;
  if (diff.length) {  // the differing blanks as runs, for diagnosis
    const runs = [];
    for (const t of diff) { if (runs.length && t === runs[runs.length - 1][1] + 1) runs[runs.length - 1][1] = t; else runs.push([t, t]); }
    console.log(`     differing blanks: ${diff.length}, in ${runs.length} runs: ${runs.slice(0, 12).map(([a, b]) => a === b ? a : `${a}-${b}`).join(', ')}${runs.length > 12 ? ' ...' : ''}`);
    const t0 = diff[0];
    for (const t of [diff[0], diff[1], diff[2], diff[3]].filter((x) => x !== undefined)) {
      const hh = h.get(t), jj = j.get(t);
      console.log(`     at ${t}: differing fields ` + hh.map((v, k) => (v !== jj[k] ? `${v} / ${jj[k]}` : null)).filter(Boolean).join(', '));
    }
  }
  check(both.length >= MIN_BLANKS, `${name}: ${h.size} / ${j.size} blanks written (${(both.length / SECONDS).toFixed(1)} a second), ${both.length} in both (at least ${MIN_BLANKS})`);
  check(diff.length === 0, `${name}: host = join, whole state, at every one of ${both.length} blanks (${fight} in a fight)` + (diff.length ? `; first difference at blank ${diff[0]}` : ''));
  check(!logs.some((l) => l.includes('no longer agree')), `${name}: no desync reported`);
  const connected = logs.map((l) => (l.match(/bt3: net: connected[^\n]*/) || ['(no "connected" line)'])[0]);
  console.log(`     host: ${connected[0]}\n     join: ${connected[1]}\n     packets host ${stats[0]}, join ${stats[1]}`);
  return { h, fight };
}

async function offlineDemo() {
  console.log('--- offline demo (one page, no network)');
  const p = await page(['BT3_DEMO=1', 'BT3_HASH=/net.hash', 'BT3_SOUND_TICKS=1'], false);
  await sleep(SECONDS * 1000);
  const h = await hashes(p);
  await p.b.close();
  return h;
}

(async () => {
  if (process.env.O2_ONLY === 'clean') {  // (diagnosis: the first scenario alone)
    await scenario('clean, delay 2', ['BT3_NET_DELAY=2']);
    console.log(failed ? `RESULT: FAIL (${failed})` : 'RESULT: PASS');
    process.exit(failed ? 1 : 0);
  }
  await scenario('clean, delay 2', ['BT3_NET_DELAY=2']);
  await scenario('10% loss, 100 ms latency, delay 2', ['BT3_NET_DELAY=2', 'BT3_NET_LOSS=10', 'BT3_NET_LATENCY=100']);
  const demo = await scenario('attract demo fight, delay 2', ['BT3_NET_DELAY=2', 'BT3_DEMO=1'], { script: false });
  const off = await offlineDemo();
  const both = [...demo.h.keys()].filter((t) => off.has(t)).sort((a, b) => a - b);
  const diff = both.filter((t) => demo.h.get(t).slice(2).join(' ') !== off.get(t).slice(2).join(' '));
  const fight = both.filter((t) => off.get(t)[2] !== ZERO).length;
  check(fight >= MIN_FIGHT && diff.length === 0, `demo fight over the network = the same fight offline, fight values at every one of ${both.length} blanks (${fight} in a fight)` + (diff.length ? `; first difference at blank ${diff[0]}` : ''));
  console.log(failed ? `RESULT: FAIL (${failed})` : 'RESULT: PASS (two pages on one machine; no real network)');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
