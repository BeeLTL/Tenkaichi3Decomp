// What the page draws over and beside the picture in place of the native window (port/web/screen_overlay.js,
// net_overlay.js's meter), in Chrome through the DevTools protocol:
//   - the meter (setting "meter"): the pictures a second and the game's speed;
//   - the side bars (setting "side_bars"): in touch play on a wide screen, upstream's art at both sides of the picture,
//     touching it; none with the setting off;
//   - the names of a stage and a song added from outside the disc, over the Map Select and the BGM Select, reached
//     through the game's own menus with the keyboard (Main Menu > Duel > 1P VS COM, two fighters, the map, the music
//     list), as the page draws them: the name, inside the picture, where the game's name band is.
//   node port/tests/web/screen_overlay.js --data <gamedata folder>
// It starts its own development server (port TEST_PORT) with a temporary folder mounted at /gamedata/test_extras,
// holding a copy of one of the disc's stage models and of one of its ADX streams under new names (copied from your
// own data folder into the system's temporary folder, removed at the end; nothing is published).
'use strict';
const fs = require('fs'), os = require('os'), path = require('path');
const { spawn } = require('child_process');
const { launch, evaluate, sleep } = require('../lib/cdp.js');

const args = Object.fromEntries(process.argv.slice(2).reduce((a, x, i, all) => (x.startsWith('--') ? [...a, [x.slice(2), all[i + 1]]] : a), []));
const ROOT = path.resolve(__dirname, '../../..');
const TEST_PORT = 8092;
const STAGE_SOURCE = 'pzs3us1/00368.bin', SONG_SOURCE = 'disc/DATA/ZS3USOP.ADX';
const KEYS = { Enter: ['Enter', 'Enter', 13], K: ['KeyK', 'k', 75], L: ['KeyL', 'l', 76], Up: ['ArrowUp', 'ArrowUp', 38], Down: ['ArrowDown', 'ArrowDown', 40],
  Left: ['ArrowLeft', 'ArrowLeft', 37] };
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failed++; };

if (!args.data || !fs.existsSync(path.join(args.data, STAGE_SOURCE)) || !fs.existsSync(path.join(args.data, SONG_SOURCE))) {
  console.error(`usage: node port/tests/web/screen_overlay.js --data <gamedata folder> (it needs ${STAGE_SOURCE} and ${SONG_SOURCE} there)`);
  process.exit(2);
}

(async () => {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'bt3-overlay-'));
  fs.mkdirSync(path.join(tmp, 'stages'));
  fs.mkdirSync(path.join(tmp, 'songs'));
  fs.copyFileSync(path.join(args.data, STAGE_SOURCE), path.join(tmp, 'stages', 'Test Stage.unk'));
  fs.copyFileSync(path.join(args.data, SONG_SOURCE), path.join(tmp, 'songs', 'Test Song.adx'));
  const server = spawn(process.env.PYTHON || 'python', [path.join(ROOT, 'port/web/devserver.py'), '--port', String(TEST_PORT),
    '--root', path.join(ROOT, 'port/build/web'), '--mount', `gamedata=${args.data}`, '--mount', `gamedata/test_extras=${tmp}`],
    { stdio: 'ignore', env: { ...process.env, DEVSERVER_QUIET: '1' } });
  const b = await launch({ headless: true });
  try {
    await sleep(1500);
    const ev = (e) => evaluate(b.send, e);
    const get = (n) => ev(`bt3Module.ccall('Bt3Ui_Get','number',['string'],['${n}'])`);
    const set = (n, v) => ev(`bt3Module.ccall('Bt3Ui_Set','number',['string','number'],['${n}',${v}])`);
    const key = async (k, times = 1) => {
      for (let t = 0; t < times; t++) {
        const [code, kk, vk] = KEYS[k];
        await b.send('Input.dispatchKeyEvent', { type: 'rawKeyDown', code, key: kk, windowsVirtualKeyCode: vk });
        await sleep(250);
        await b.send('Input.dispatchKeyEvent', { type: 'keyUp', code, key: kk, windowsVirtualKeyCode: vk });
        await sleep(600);
      }
    };
    await b.send('Emulation.setDeviceMetricsOverride', { width: 1600, height: 800, deviceScaleFactor: 1, mobile: false });
    await b.send('Page.enable');
    await b.send('Page.navigate', { url: `http://127.0.0.1:${TEST_PORT}/index.html?autostart=1&env=BT3_NOMOVIE=1,` +
      'BT3_STAGES=gamedata/test_extras/stages,BT3_SONGS=gamedata/test_extras/songs' });
    for (let i = 0; i < 240 && !(await ev(`!!(globalThis.settings && settings.engineReady)`).catch(() => false)); i++) await sleep(250);
    await set('skip_copyright', 1);
    await ev(`document.getElementById('canvas').focus(), 0`);

    // the meter
    await set('meter', 1);
    await sleep(2000);
    const meter = await ev(`document.getElementById('net-meter').hidden ? '' : document.getElementById('net-meter').textContent`);
    check(/^\d+ fps {3}speed \d+%$/.test(meter) && Number(meter.match(/speed (\d+)/)[1]) > 80, `the meter: "${meter}"`);
    await set('meter', 0);

    // the side bars, in touch play (the picture fitted to a 2:1 screen)
    // (what is on the screen: an element counts as drawn when the browser shows it, whatever its hidden flag says)
    const bars = () => ev(`JSON.stringify([...document.querySelectorAll('.bt3-bar')].map((e) => ({ hidden: getComputedStyle(e).display === 'none', ...e.getBoundingClientRect().toJSON() })))`).then(JSON.parse);
    const picture = () => ev(`(() => { const p = settings.screenOverlay.pictureBox(), st = settings.stage.getBoundingClientRect();
      return JSON.stringify({ ...p, x: p.x + st.left, y: p.y + st.top }); })()`).then(JSON.parse);  // (in the page's coordinates)
    await ev(`document.body.classList.add('touch-play'), 0`);
    await sleep(800);
    check((await bars()).every((x) => x.hidden), 'side bars off: none drawn');
    await set('side_bars', 1);
    await sleep(800);
    const [l, r] = await bars(), p = await picture();
    check(!l.hidden && !r.hidden && Math.abs(l.right - p.x) < 2 && Math.abs(r.left - (p.x + p.w)) < 2 && l.width > 100 && Math.abs(l.height - p.h) < 2,
          `side bars on: both drawn, touching the picture (left ${l.left.toFixed(0)}-${l.right.toFixed(0)}, picture ${p.x.toFixed(0)}-${(p.x + p.w).toFixed(0)}, right ${r.left.toFixed(0)}-${r.right.toFixed(0)})`);
    check(await ev(`[...document.images].length >= 0 && (async () => { const i = new Image(); i.src = 'bars/trim.png'; await i.decode(); return i.naturalWidth === 34; })()`),
          'the art is served (bars/trim.png, 34 x 720)');
    await set('side_bars', 0);
    await ev(`document.body.classList.remove('touch-play'), 0`);

    // the names: through the menus to the Map Select, the added stage (the last row), the music list, the added song
    for (let i = 0; i < 480 && (await get('frame')) < 2050; i++) await sleep(250);
    await key('Enter', 3); await sleep(2500); await key('K', 2); await sleep(1500); await key('Left'); await key('K'); await sleep(7000);
    // (the waits are the ones the menus were seen to need: a press during a screen's change is lost or lands elsewhere)
    await key('Down', 3); await sleep(1500); await key('K'); await sleep(6500);  // Main Menu > Duel
    await key('K'); await sleep(6500);                                         // 1P VS COM
    for (const ms of [5500, 4000, 4000, 4000, 4500, 5500, 5500]) { await key('K'); await sleep(ms); }  // the two fighters, colours, to the Map Select
    const name = (kind) => ev(`(() => { const e = settings.screenOverlay.names['${kind}']; return getComputedStyle(e).display === 'none' ? null : JSON.stringify({ text: e.textContent, ...e.getBoundingClientRect().toJSON() }); })()`).then((x) => (x ? JSON.parse(x) : null));
    const waitName = async (kind) => { for (let i = 0; i < 12; i++) { const n = await name(kind); if (n) return n; await sleep(250); } return null; };
    const shotAs = async (tag) => {
      const sh = await b.send('Page.captureScreenshot', { format: 'png' });
      fs.writeFileSync(path.join(os.tmpdir(), `screen_overlay_${tag}.png`), Buffer.from(sh.data, 'base64'));
      console.log(`     (the screen: ${path.join(os.tmpdir(), `screen_overlay_${tag}.png`)})`);
    };
    await key('Up'); await sleep(2500);  // (the added stage: the last row)
    const st = await waitName('stage');
    await shotAs('stage');
    const pic = await picture();
    check(st && st.text === 'Test Stage' && st.left >= pic.x && st.right <= pic.x + pic.w + 1 && st.top > pic.y + pic.h * 0.4 && st.bottom < pic.y + pic.h * 0.8,
          `the added stage's name over the Map Select: ${st ? `"${st.text}" at ${st.left.toFixed(0)},${st.top.toFixed(0)} ${st.width.toFixed(0)}x${st.height.toFixed(0)}` : 'not drawn'}`);
    await key('Down'); await sleep(3000);  // (back to the first row: the disc's stages)
    check(!(await name('stage')), 'a stage of the disc chosen: the name of the added stage is gone from the screen');
    await key('Up'); await sleep(3000);
    check(!!(await waitName('stage')), 'the added stage chosen again: its name is back');
    await key('Left'); await sleep(3500); await key('K'); await sleep(4500);  // the music row, its list
    await key('Up'); await sleep(3500); await key('Up'); await sleep(3500);  // (Random, then the added song: the last two)
    const so = await waitName('song');
    check(so && so.text === 'Test Song' && so.top > pic.y + pic.h * 0.6, `the added song's name in the BGM Select: ${so ? `"${so.text}" at ${so.left.toFixed(0)},${so.top.toFixed(0)}` : 'not drawn'}`);
    await shotAs('song');
    await key('Up'); await sleep(3500);  // (a song of the disc)
    check(!(await name('song')) && !!(await name('stage')), 'a song of the disc chosen: the name of the added song is gone from the screen, that of the stage stays');
    check((await bars()).every((x) => x.hidden), 'no side bar on the screen outside touch play and full screen (side bars off)');
    await shotAs('after');
  } finally {
    await b.close();
    server.kill();
    fs.rmSync(tmp, { recursive: true, force: true });
  }
  console.log(failed ? `RESULT: FAIL (${failed})` : 'RESULT: PASS');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
