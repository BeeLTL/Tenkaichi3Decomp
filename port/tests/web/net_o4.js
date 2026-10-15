// Online play in the browser, test O4 on one machine (docs/BROWSER_ONLINE_PLAN.md, P4): two pages meet by ROOM CODE
// through the signalling Worker (port/web/signal/worker.js, run here by local.mjs over SQLite), with nothing passed
// by hand. Two headless Chrome instances, each page told the Worker's address with ?signal=. Per cycle:
//   1. both pages run the game; the host makes a room with net.c's own room-code lobby (Port_LobbyStartCode, through
//      net_web.c's Match_* and net_web.js) and gets a code; the other page joins with that code;
//   2. the two pages' offer and answer pass through the Worker; net.c's lobby greets over the connection (both 2);
//   3. both launch the session: the pages load themselves again, and connect again THROUGH THE SAME ROOM (round 2),
//      on their own; both reach the character select with no desync; the room is given up once they are connected;
//   4. backing out on the host ends the session on both.
// Then the errors the game's window must be able to show: a code that names no room, a room given up (cancel), a page
// with no signalling service set.
// What this does not show: two machines, different networks, NAT, the TURN relay (O4 proper needs those).
//   node port/tests/web/net_o4.js [cycles, default 3]
// Needs the dev server on 127.0.0.1:8080 with the game data.
'use strict';
const path = require('path');
const { launch, evaluate, sleep } = require('../lib/cdp.js');

const SIGNAL_PORT = 8787;
const SIGNAL = `http://127.0.0.1:${SIGNAL_PORT}`;
const URL = `http://127.0.0.1:8080/index.html?autostart=1&signal=${encodeURIComponent(SIGNAL)}`;
const CYCLES = Number(process.argv[2] || 3);
const ARGS = ['--disable-features=WebRtcHideLocalIpsWithMdns'];
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failed++; };

async function open(name) {
  const b = await launch({ headless: true, extraArgs: ARGS });
  const ev = (e) => evaluate(b.send, e);
  await b.send('Page.enable');
  await b.send('Page.navigate', { url: URL });
  return { name, b, ev, async key(code, key, vk) {
    await b.send('Input.dispatchKeyEvent', { type: 'rawKeyDown', code, key, windowsVirtualKeyCode: vk });
    await sleep(120);
    await b.send('Input.dispatchKeyEvent', { type: 'keyUp', code, key, windowsVirtualKeyCode: vk });
  } };
}
const get = (p, k) => p.ev(`(globalThis.settings && settings.engineReady) ? bt3Module.ccall('Bt3Ui_Get','number',['string'],['${k}']) : -1`).catch(() => -1);
const poll = (p) => p.ev(`bt3Module.ccall('Bt3Ui_NetLobbyPoll','number',[],[])`);
const codeOf = (p) => p.ev(`(bt3Module.ccall('Bt3Ui_NetLobbyPoll','number',[],[]), bt3Module.ccall('Bt3Ui_NetCode','string',[],[]))`);  // (the code is taken from the service by a poll)
const errorOf = (p) => p.ev(`bt3Module.ccall('Bt3Ui_NetError','string',[],[])`);
const start = (p, host, code) => p.ev(`bt3Module.ccall('Bt3Ui_NetCodeStart','number',['number','string'],[${host},${JSON.stringify(code)}])`);
const cancel = (p) => p.ev(`bt3Module.ccall('Bt3Ui_NetLobbyCancel',null,[],[])`);
async function until(what, fn, ms) {
  for (const end = Date.now() + ms; Date.now() < end; await sleep(250)) if (await fn().catch(() => false)) return true;
  console.log(`     (gave up waiting: ${what})`);
  return false;
}
async function reloaded(p, ms) {
  return until(`${p.name} loaded again`, async () => !(await p.ev(`!!globalThis.__o4mark`)) && (await p.ev(`!!(globalThis.settings && settings.engineReady)`)), ms);
}
const log = (p) => p.ev(`document.getElementById('log').textContent`);

(async () => {
  const { serve } = await import(require('url').pathToFileURL(path.join(__dirname, '../../web/signal/local.mjs')).href);
  const server = await serve(SIGNAL_PORT);
  let requests = 0;
  server.on('request', () => { requests++; });
  const host = await open('host'), join = await open('join');
  for (let c = 1; c <= CYCLES; c++) {
    console.log(`--- cycle ${c}`);
    check(await until('both games running', async () => (await get(host, 'frame')) > 300 && (await get(join, 'frame')) > 300, 60000), `cycle ${c}: both games run`);
    // 1-2. a room code, and the lobby over the connection it brings
    const t0 = Date.now(), r0 = requests;
    await start(host, 1, '');
    let code = '';
    await until('the room code', async () => /^[A-Z2-9]{6}$/.test(code = await codeOf(host)), 15000);
    check(/^[A-Z2-9]{6}$/.test(code), `cycle ${c}: the host has a room code (${code}) after ${Date.now() - t0} ms`);
    await start(join, 0, code.toLowerCase());
    let ph = 0, pj = 0;
    await until('the lobby', async () => { ph = await poll(host); pj = await poll(join); return ph === 2 && pj === 2; }, 30000);
    check(ph === 2 && pj === 2, `cycle ${c}: joined by code; the lobby greets over the connection (host ${ph}, join ${pj}) after ${Date.now() - t0} ms, ${requests - r0} requests to the Worker`);
    if (ph !== 2 || pj !== 2) {
      console.log(`     host: ${await errorOf(host)}
     join: ${await errorOf(join)}`);
      break;  // (no session to start)
    }
    // 3. launch: the pages load again and connect again through the room by themselves
    await host.ev(`globalThis.__o4mark = 1`); await join.ev(`globalThis.__o4mark = 1`);
    await host.ev(`bt3Module.ccall('Bt3Ui_Action','number',['string','number'],['net_launch',0])`);
    await join.ev(`bt3Module.ccall('Bt3Ui_Action','number',['string','number'],['net_launch',0])`);
    check((await reloaded(host, 30000)) && (await reloaded(join, 30000)), `cycle ${c}: both pages loaded again for the session`);
    const t1 = Date.now();
    check(await until('connected again', async () => (await host.ev(`bt3Net.open`)) && (await join.ev(`bt3Net.open`)), 30000),
          `cycle ${c}: connected again through the room, by themselves (round ${await join.ev(`bt3Net.room ? bt3Net.room.round : 2`)}), ${Date.now() - t1} ms after the reload`);
    const arrived = await until('the character select', async () => (await get(host, 'net_arrived')) === 1 && (await get(join, 'net_arrived')) === 1, 120000);
    const [lh, lj] = [await log(host), await log(join)];
    check(arrived && /started again for a session/.test(lh) && /started again for a session/.test(lj), `cycle ${c}: both in the session at the character select`);
    check(!/no longer agree/.test(lh + lj), `cycle ${c}: no desync`);
    check(await until('the room given up', async () => (await host.ev(`bt3Net.room === null`)), 10000), `cycle ${c}: the host gave the room up once connected`);
    // 4. back out on the host: the session ends on both
    await host.ev(`globalThis.__o4mark = 1`); await join.ev(`globalThis.__o4mark = 1`);
    await host.ev(`document.getElementById('canvas').focus(), 0`);
    let back = false;
    for (let i = 0; i < 60 && !back; i++) {
      if (i % 2) await host.key('KeyI', 'i', 73); else await host.key('KeyL', 'l', 76);
      await sleep(700);
      back = !(await host.ev(`!!globalThis.__o4mark`).catch(() => false));
    }
    check((await reloaded(host, 30000)) && (await reloaded(join, 30000)) &&
          (await until('no session', async () => (await get(host, 'net_session')) === 0 && (await get(join, 'net_session')) === 0, 30000)),
          `cycle ${c}: backing out ended the session; both back without one`);
  }
  // errors, as the game's window gets them
  console.log('--- errors');
  await until('both games running', async () => (await get(host, 'frame')) > 300 && (await get(join, 'frame')) > 300, 60000);
  const failure = async (p, what) => { let s = 0; await until(what, async () => (s = await poll(p)) < 0, 15000); return [s, await errorOf(p)]; };
  await start(join, 0, 'ZZZZZZ');
  let [s, e] = await failure(join, 'a wrong code refused');
  check(s === -3 && /No room with that code/.test(e), `a code that names no room: ${s}, "${e}"`);
  await cancel(join);
  await start(host, 1, '');
  let code = '';
  await until('a room code', async () => /^[A-Z2-9]{6}$/.test(code = await codeOf(host)), 15000);
  await cancel(host);
  await sleep(3000);  // (a room the game is done with is given up a moment later: net_web.js NET_LEAVE_MS)
  await start(join, 0, code);
  [s, e] = await failure(join, 'a cancelled room refused');
  check(s === -3 && /No room with that code/.test(e), `a room given up (cancel) cannot be joined: ${s}, "${e}"`);
  await cancel(join);
  await join.ev(`bt3Net.signalUrl = ''`);
  await start(join, 0, 'ABCDEF');
  [s, e] = await failure(join, 'no service refused');
  check(s === -3 && /signalling service/.test(e), `no signalling service set: ${s}, "${e}"`);
  await cancel(join);
  await join.ev(`bt3Net.signalUrl = ${JSON.stringify(SIGNAL)}`);
  await host.b.close(); await join.b.close();
  server.close();
  console.log(failed ? `RESULT: FAIL (${failed})` : 'RESULT: PASS (two pages on one machine, the Worker run locally; no real network, no NAT, no relay)');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
