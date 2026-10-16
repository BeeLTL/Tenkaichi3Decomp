// Online play in the browser, test O5 (docs/BROWSER_ONLINE_PLAN.md, P5): two pages play online through the GAME'S OWN
// menu, as players would, with the keyboard only: Main Menu > Tenkaichi Budokai Online (upstream's screens,
// src/menu/port_tbo.c) > Create Room > Open Room on one page, Join Room and its keypad on the other. Through the
// signalling Worker (port/web/signal, run here by local.mjs). Per cycle:
//   1. both pages start the game and go to Tenkaichi Budokai Online (its words are drawn: the fonts are there);
//   2. the host opens a room: its code comes from the Worker, and "Copy Code" (Cross) reaches the page (a notice with
//      the code: the system clipboard where the browser allows it);
//   3. the other page joins on the keypad: cycle 1 types the code (the keyboard's letters, web_ui.c's text input),
//      cycle 2 pastes it (Ctrl+V: the page takes the browser's paste, Bt3Ui_TboPaste); then Enter;
//   4. both pages load again for the session, connect again through the room, reach the character select, agree;
//   5. backing out on the host ends the session on both.
// The gameplay fingerprint (online play's "two unmodded games") comes from the data server (devserver.py).
// What this does not show: two machines, NAT, the relay; a controller (the keyboard plays the pad here).
//   node port/tests/web/net_o5.js
// Needs the dev server on 127.0.0.1:8080 with the game data (original disc).
'use strict';
const path = require('path'), fs = require('fs'), os = require('os');
const { launch, evaluate, sleep } = require('../lib/cdp.js');

const SIGNAL_PORT = 8788;
const ORIGIN = 'http://127.0.0.1:8080';
const URL = `${ORIGIN}/index.html?autostart=1&console=1&signal=${encodeURIComponent(`http://127.0.0.1:${SIGNAL_PORT}`)}&env=BT3_NOMOVIE=1`;
const ARGS = ['--disable-features=WebRtcHideLocalIpsWithMdns'];
const KEYS = { Enter: ['Enter', 'Enter', 13], K: ['KeyK', 'k', 75], L: ['KeyL', 'l', 76], I: ['KeyI', 'i', 73],
  Down: ['ArrowDown', 'ArrowDown', 40], Left: ['ArrowLeft', 'ArrowLeft', 37], Backspace: ['Backspace', 'Backspace', 8] };
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failed++; };

async function open(name) {
  const b = await launch({ headless: true, extraArgs: ARGS });
  const ev = (e) => evaluate(b.send, e);
  await b.send('Browser.grantPermissions', { origin: ORIGIN, permissions: ['clipboardReadWrite', 'clipboardSanitizedWrite'] }).catch(() => {});
  const lines = [];  // every line of the page's log, across its reloads (?console=1: each is copied to the console)
  b.onEvent((m) => {
    if (m.method === 'Runtime.consoleAPICalled') lines.push(m.params.args.map((a) => a.value ?? a.description ?? '').join(' '));
  });
  await b.send('Runtime.enable');
  await b.send('Page.enable');
  await b.send('Page.navigate', { url: URL });
  const p = { name, b, ev, lines };
  p.key = async (k, times = 1) => {
    for (let t = 0; t < times; t++) {
      const [code, key, vk] = KEYS[k];
      await b.send('Input.dispatchKeyEvent', { type: 'rawKeyDown', code, key, windowsVirtualKeyCode: vk });
      await sleep(200);
      await b.send('Input.dispatchKeyEvent', { type: 'keyUp', code, key, windowsVirtualKeyCode: vk });
      await sleep(500);
    }
  };
  p.type = async (text) => {  // as a keyboard types it: a key with its character (SDL's text input)
    for (const ch of text) {
      const code = /[0-9]/.test(ch) ? `Digit${ch}` : `Key${ch.toUpperCase()}`;
      await b.send('Input.dispatchKeyEvent', { type: 'keyDown', code, key: ch, text: ch, windowsVirtualKeyCode: ch.toUpperCase().charCodeAt(0) });
      await b.send('Input.dispatchKeyEvent', { type: 'keyUp', code, key: ch, windowsVirtualKeyCode: ch.toUpperCase().charCodeAt(0) });
      await sleep(150);
    }
  };
  p.paste = async (text) => {  // Ctrl+V with `text` on the clipboard
    await ev(`navigator.clipboard.writeText(${JSON.stringify(text)})`);
    await b.send('Input.dispatchKeyEvent', { type: 'rawKeyDown', code: 'KeyV', key: 'v', windowsVirtualKeyCode: 86, modifiers: 2, commands: ['paste'] });
    await b.send('Input.dispatchKeyEvent', { type: 'keyUp', code: 'KeyV', key: 'v', windowsVirtualKeyCode: 86, modifiers: 2 });
  };
  p.shot = async (tag) => {
    const s = await b.send('Page.captureScreenshot', { format: 'png' });
    const f = path.join(os.tmpdir(), `o5_${name}_${tag}.png`);
    fs.writeFileSync(f, Buffer.from(s.data, 'base64'));
    console.log(`     (${name}'s screen: ${f})`);
  };
  return p;
}
const get = (p, k) => p.ev(`(globalThis.settings && settings.engineReady) ? bt3Module.ccall('Bt3Ui_Get','number',['string'],['${k}']) : -1`).catch(() => -1);
async function until(what, fn, ms) {
  for (const end = Date.now() + ms; Date.now() < end; await sleep(250)) if (await fn().catch(() => false)) return true;
  console.log(`     (gave up waiting: ${what})`);
  return false;
}
const log = (p) => p.ev(`document.getElementById('log').textContent`);

// From the page's start to Tenkaichi Budokai Online's hub (Create Room selected): the title (START), New Game without
// a save (Yes), the main menu's fifth item.
async function toHub(p) {
  await until(`${p.name}: the engine`, () => p.ev(`!!(globalThis.settings && settings.engineReady)`), 60000);
  const skip = process.env.O5_SKIP_COPYRIGHT === '1';  // (the Extras setting "Skip copyright page": a shorter start)
  await p.ev(`bt3Module.ccall('Bt3Ui_Set','number',['string','number'],['skip_copyright',${skip ? 1 : 0}])`);
  await p.ev(`document.getElementById('canvas').focus(), 0`);
  await until(`${p.name}: the title`, async () => (await get(p, 'frame')) > (skip ? 2050 : 2400), 120000);
  await p.key('Enter', 3);
  await sleep(2500);
  await p.key('K', 2);
  await sleep(1500);
  await p.key('Left'); await p.key('K');
  await sleep(7000);
  await p.key('Down', 4); await p.key('K');
  await sleep(4000);
}

(async () => {
  const { serve } = await import(require('url').pathToFileURL(path.join(__dirname, '../../web/signal/local.mjs')).href);
  const server = await serve(SIGNAL_PORT);
  for (const [c, how] of [[1, 'typed'], [2, 'pasted']]) {
    console.log(`--- cycle ${c}: the code ${how}`);
    const host = await open('host'), join = await open('join');
    await Promise.all([toHub(host), toHub(join)]);
    // 2. the host: Create Room, Open Room; its code; Copy Code
    await host.key('K'); await sleep(2500); await host.key('K');
    let code = '';
    const coded = await until('the room code', async () => /^[A-Z2-9]{6}$/.test(code = await host.ev(`bt3Module.ccall('Bt3Ui_NetCode','string',[],[])`)), 20000);
    check(coded, `cycle ${c}: the host opened a room from the menu: code ${code}`);
    check(/gameplay data: afd9336a.*the original disc's \(worked out by the data server\)/.test(await log(host)),
          `cycle ${c}: the menu let the host online: the gameplay fingerprint is the original disc's (from the data server)`);
    if (!coded) { await host.shot(`c${c}_nocode`); }
    await host.key('K');  // Copy Code
    const noticed = await until('the copy notice', async () => (await host.ev(`document.getElementById('notice').textContent`)).includes(code), 5000);
    const notice = await host.ev(`document.getElementById('notice').textContent`);
    const clip = await host.ev(`navigator.clipboard.readText().catch(() => '')`);
    check(coded && noticed, `cycle ${c}: Copy Code reaches the page ("${notice}"; the system clipboard has ${JSON.stringify(clip)})`);
    // 3. the other page: Join Room, the keypad
    await join.key('Down'); await join.key('K');
    const typing = await until('the keypad', async () => (await join.ev(`bt3Module.ccall('Bt3Ui_TboTyping','number',[],[])`)) === 1, 8000);
    check(typing, `cycle ${c}: Join Room's keypad takes text`);
    if (how === 'typed') {
      await join.type('QQ'); await join.key('Backspace', 2);  // (typed and taken back: the keypad's own delete)
      await join.type(code.toLowerCase());
    } else {
      await join.paste(code);
    }
    await sleep(800);
    await join.shot(`c${c}_keypad`);
    await join.ev(`globalThis.__o5mark = 1`); await host.ev(`globalThis.__o5mark = 1`);
    await join.key('Enter');
    // 4. the session
    const reloaded = async (p) => until(`${p.name} loaded again`, async () => !(await p.ev(`!!globalThis.__o5mark`)) && (await p.ev(`!!(globalThis.settings && settings.engineReady)`)), 60000);
    const re = (await reloaded(host)) && (await reloaded(join));
    check(re, `cycle ${c}: joined with the ${how} code; both pages loaded again for the session`);
    if (!re) { await host.shot(`c${c}_host`); await join.shot(`c${c}_join`); }
    const kept = { host: '', join: '' };  // the session pages' own logs (gone once a page loads again)
    const keptStats = { host: '', join: '' };
    const keep = async (p) => {
      const l = await log(p).catch(() => '');
      if (/started again for a session/.test(l)) {
        kept[p.name] = l;
        if (!keptStats[p.name]) keptStats[`${p.name}Env`] = await p.ev(`JSON.stringify(Object.fromEntries(Object.entries(bt3Module.ENV).filter(([k]) => k.startsWith('BT3_'))))`).catch(() => '?');
        keptStats[p.name] = `open ${await p.ev('bt3Net.open').catch(() => '?')}, stats ${await p.ev('JSON.stringify(bt3Net.stats)').catch(() => '?')}, queued ${await p.ev('bt3Net.queue.length').catch(() => '?')}, frame ${await get(p, 'frame')}, session ${await get(p, 'net_session')}, active ${await get(p, 'net_active')}`;
      }
    };
    let lastSnap = Date.now();
    const arrived = re && await until('the character select', async () => { await keep(host); await keep(join);
      if (process.env.O5_TIMELINE && Date.now() - lastSnap > 10000) {  // (diagnosis: what each page does, every 10 s)
        lastSnap = Date.now();
        for (const p of [host, join]) {
          const l = (await log(p).catch(() => '')).split(String.fromCharCode(10)).filter(Boolean);
          console.log(`     t ${p.name}: ${await p.ev('JSON.stringify(bt3Net.stats)').catch(() => '?')} frame ${await get(p, 'frame')} | ${l[l.length - 1]}`);
        }
      }
      return (await get(host, 'net_arrived')) === 1 && (await get(join, 'net_arrived')) === 1; }, 120000);
    const [lh, lj] = [await log(host), await log(join)];
    if (re && !arrived) {  // (why: the two screens, the connection, net.c's lines)
      await host.shot(`c${c}_noarrive`); await join.shot(`c${c}_noarrive`);
      for (const [p, l] of [[host, lh], [join, lj]]) {
        console.log(`     ${p.name} (the session page, last seen): ${keptStats[p.name]}`);
        console.log(`       env ${keptStats[`${p.name}Env`]}`);
        const from = p.lines.map((x, i) => (/started again for a session/.test(x) ? i : -1)).filter((i) => i >= 0).pop() ?? 0;
        fs.writeFileSync(path.join(os.tmpdir(), `o5_${p.name}_c${c}_console.txt`), p.lines.slice(from).join(String.fromCharCode(10)));
        for (const line of p.lines.slice(from).filter((x) => /net|session|lobby|exit|abort|error|quit|leave|gone|notice/i.test(x)).slice(0, 40)) console.log(`       ${line}`);
      }
    }
    check(arrived && /started again for a session/.test(lh) && /started again for a session/.test(lj), `cycle ${c}: both in the session at the character select`);
    check(!/no longer agree/.test(lh + lj), `cycle ${c}: no desync`);
    // 5. back out on the host
    if (arrived) {
      await host.ev(`globalThis.__o5mark = 1`); await join.ev(`globalThis.__o5mark = 1`);
      await host.ev(`document.getElementById('canvas').focus(), 0`);
      let back = false;
      for (let i = 0; i < 60 && !back; i++) {
        await host.key(i % 2 ? 'I' : 'L');
        await sleep(500);
        back = !(await host.ev(`!!globalThis.__o5mark`).catch(() => false));
      }
      check(back && (await reloaded(host)) && (await reloaded(join)) &&
            (await until('no session', async () => (await get(host, 'net_session')) === 0 && (await get(join, 'net_session')) === 0, 30000)),
            `cycle ${c}: backing out ended the session; both back without one`);
    }
    await host.b.close(); await join.b.close();
  }
  server.close();
  console.log(failed ? `RESULT: FAIL (${failed})` : 'RESULT: PASS (two pages on one machine, the keyboard as the pad, the Worker run locally; no real network)');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
