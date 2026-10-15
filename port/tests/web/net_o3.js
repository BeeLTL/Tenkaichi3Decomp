// Online play in the browser, test O3 (docs/BROWSER_ONLINE_PLAN.md, P3): a session started and ended in the page, as a
// player would, several times in a row. Two headless Chrome instances on this machine (no real network):
//   1. both pages run the game normally; the test connects them (WebRTC offer / answer, as P4's service will);
//   2. net.c's lobby (the native window's, with an address) greets over the connection: host and joiner see each other;
//   3. both launch the session: net.c starts the program again, which in the browser is the page loading itself again
//      with the session's variables (net_web.c, net_web.js); the test connects the new pages;
//   4. both reach the character select (the session's start-up is over: headless.c Port_NetArrived) and agree there;
//   5. one player backs out to the main menu (Circle, the "L" key, on the host), which ends the session on both
//      (menu_a.c Port_NetLeave; the other side is told): both pages load again as they were, without a session.
// Repeated CYCLES times; each page's memory (performance.measureUserAgentSpecificMemory) is measured at the title after
// every cycle and must not grow. During a session the player's own saves are not touched (saves.js).
//   node port/tests/web/net_o3.js [cycles, default 10]
// Needs the dev server on 127.0.0.1:8080 with the game data.
'use strict';
const { launch, evaluate, sleep } = require('../lib/cdp.js');

const URL = 'http://127.0.0.1:8080/index.html?autostart=1';
const CYCLES = Number(process.argv[2] || 10);
const ARGS = ['--disable-features=WebRtcHideLocalIpsWithMdns'];
const MEMORY_GROWTH_MAX = 1.15;  // the last cycle's memory at most 15% above the first's
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failed++; };

async function open(name) {
  const b = await launch({ headless: true, extraArgs: ARGS });
  const ev = (e) => evaluate(b.send, e);
  await b.send('Page.enable');
  await b.send('Page.navigate', { url: URL });
  return { name, b, ev, async key(code, key, vk) {
    await b.send('Input.dispatchKeyEvent', { type: 'rawKeyDown', code, key, windowsVirtualKeyCode: vk });
    await sleep(120);  // (held over a few blanks: the game reads the pad once a blank)
    await b.send('Input.dispatchKeyEvent', { type: 'keyUp', code, key, windowsVirtualKeyCode: vk });
  } };
}
const get = (p, k) => p.ev(`(globalThis.settings && settings.engineReady) ? bt3Module.ccall('Bt3Ui_Get','number',['string'],['${k}']) : -1`).catch(() => -1);
async function until(what, fn, ms) {
  for (const end = Date.now() + ms; Date.now() < end; await sleep(250)) if (await fn().catch(() => false)) return true;
  console.log(`     (gave up waiting: ${what})`);
  return false;
}
// a page that has loaded again: our mark is gone and the game runs
async function reloaded(p, ms) {
  // (the frame counter is no sign: a session's start-up runs without pictures, net.c Port_NetWarp)
  return until(`${p.name} loaded again`, async () => !(await p.ev(`!!globalThis.__o3mark`)) && (await p.ev(`!!(globalThis.settings && settings.engineReady)`)), ms);
}
async function connect(host, join) {
  const offer = await host.ev(`bt3Net.offer()`);
  await host.ev(`bt3Net.accept(${JSON.stringify(await join.ev(`bt3Net.answer(${JSON.stringify(offer)})`))})`);
  return until('the data channel', async () => (await host.ev(`bt3Net.open`)) && (await join.ev(`bt3Net.open`)), 10000);
}
const log = (p) => p.ev(`document.getElementById('log').textContent`);
const memory = (p) => p.ev(`performance.measureUserAgentSpecificMemory ? performance.measureUserAgentSpecificMemory().then((m) => m.bytes) : -1`).catch(() => -1);

(async () => {
  const host = await open('host'), join = await open('join');
  const mem = [];
  for (let c = 1; c <= CYCLES; c++) {
    console.log(`--- cycle ${c}`);
    // 1. both at the game, normal; connected
    check(await until('both games running', async () => (await get(host, 'frame')) > 300 && (await get(join, 'frame')) > 300, 60000),
          `cycle ${c}: both games run (no session: ${await get(host, 'net_session')} / ${await get(join, 'net_session')})`);
    mem.push([await memory(host), await memory(join)]);
    check(await connect(host, join), `cycle ${c}: connected`);
    // 2. the lobby
    await host.ev(`bt3Module.ccall('Bt3Ui_NetLobbyStart','number',['number'],[1])`);
    await join.ev(`bt3Module.ccall('Bt3Ui_NetLobbyStart','number',['number'],[0])`);
    const poll = (p) => p.ev(`bt3Module.ccall('Bt3Ui_NetLobbyPoll','number',[],[])`);
    let ph = 0, pj = 0;
    await until('the lobby', async () => { ph = await poll(host); pj = await poll(join); return ph === 2 && pj === 2; }, 15000);
    check(ph === 2 && pj === 2, `cycle ${c}: the lobby greets over the connection (host ${ph}, join ${pj})`);
    // 3. launch: both pages load again for the session; connect them again
    await host.ev(`globalThis.__o3mark = 1`); await join.ev(`globalThis.__o3mark = 1`);
    await host.ev(`bt3Module.ccall('Bt3Ui_Action','number',['string','number'],['net_launch',0])`);
    await join.ev(`bt3Module.ccall('Bt3Ui_Action','number',['string','number'],['net_launch',0])`);
    const re = (await reloaded(host, 30000)) && (await reloaded(join, 30000));
    check(re, `cycle ${c}: both pages loaded again for the session`);
    check(await connect(host, join), `cycle ${c}: connected again`);
    // 4. the session's start-up, to the character select
    const arrived = await until('the character select', async () => (await get(host, 'net_arrived')) === 1 && (await get(join, 'net_arrived')) === 1, 120000);
    const [lh, lj] = [await log(host), await log(join)];
    check(arrived && (await get(host, 'net_session')) === 1 && (await get(join, 'net_session')) === 1,
          `cycle ${c}: both in the session at the character select (${/started again for a session/.test(lh) && /started again for a session/.test(lj) ? 'relaunched' : 'NOT relaunched'})`);
    check(!/no longer agree/.test(lh + lj), `cycle ${c}: no desync`);
    {  // the player's saves: nothing kept by the session's pages (their card is net_session/, saves.js sessionActive)
      const own = (lh + lj).match(/saves: an online session wrote its own card/g) || [];
      check(!/saves: kept \(/.test(lh + lj), `cycle ${c}: the player's saves untouched in the session (the session wrote its own card ${own.length} times)`);
    }
    // 5. back out to the main menu on the host: the session ends on both
    await host.ev(`globalThis.__o3mark = 1`); await join.ev(`globalThis.__o3mark = 1`);
    await host.ev(`document.getElementById('canvas').focus(), 0`);
    let back = false;
    for (let i = 0; i < 60 && !back; i++) {  // back: Triangle ("I") and Circle ("L") in turn, whichever the screen takes
      if (i % 2) await host.key('KeyI', 'i', 73); else await host.key('KeyL', 'l', 76);
      await sleep(700);
      back = !(await host.ev(`!!globalThis.__o3mark`).catch(() => false));
    }
    if (!back) {  // (what the host shows, to see why)
      const shot = await host.b.send('Page.captureScreenshot', { format: 'png' });
      require('fs').writeFileSync(require('path').join(require('os').tmpdir(), `o3_cycle${c}_host.png`), Buffer.from(shot.data, 'base64'));
      console.log(`     (the host's screen: ${require('path').join(require('os').tmpdir(), `o3_cycle${c}_host.png`)})`);
    }
    const out = (await reloaded(host, 30000)) && (await reloaded(join, 30000));
    check(out, `cycle ${c}: backing out ended the session; both pages loaded again`);
    check(await until('no session', async () => (await get(host, 'net_session')) === 0 && (await get(join, 'net_session')) === 0, 30000),
          `cycle ${c}: both back without a session (${/started again after a session/.test(await log(host)) ? 'relaunched' : 'NOT relaunched'})`);
  }
  mem.push([await memory(host), await memory(join)]);
  console.log(`     memory at the title, MB (host / join): ${mem.map(([a, b]) => `${(a / 1048576).toFixed(0)}/${(b / 1048576).toFixed(0)}`).join(', ')}`);
  const ok = mem[0][0] > 0 && mem.every(([a, b]) => a <= mem[0][0] * MEMORY_GROWTH_MAX && b <= mem[0][1] * MEMORY_GROWTH_MAX);
  check(ok, `memory does not grow over ${CYCLES} sessions (at most ${Math.round((MEMORY_GROWTH_MAX - 1) * 100)}% above the first)`);
  await host.b.close(); await join.b.close();
  console.log(failed ? `RESULT: FAIL (${failed})` : 'RESULT: PASS (two pages on one machine; no real network)');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
