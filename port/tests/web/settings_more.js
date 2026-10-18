// The native window's settings the browser panel gained after P5 (settings.js buildFonts / buildSticks, the Video
// tab's late_input): each one set in the panel reaches the engine, is kept for the next start, and does what it does.
//   - Menu font / Dialogue font: the lists are the engine's (fonts.tsv; "Original" first); a choice is the engine's;
//   - Low input lag: on in the engine;
//   - a stick's deadzone: with a stick held at 61% to the right (BT3_STICK_TEST: SDL's virtual controller, as upstream
//     tests it), "Original" passes it on (the game's own 39% per axis) and 80% keeps it from the game;
//   - vibration: Test reaches the controller's motors at the strength set (BT3_RUMBLE_LOG); off, Test is off too.
// Each is checked after a reload too (the page keeps the settings file: localStorage 'bt3.settings').
//   node port/tests/web/settings_more.js
// Needs the dev server on 127.0.0.1:8080 with the game data.
'use strict';
const { launch, evaluate, sleep } = require('../lib/cdp.js');

const PAGE = 'http://127.0.0.1:8080/index.html?autostart=1&env=BT3_STICK_TEST=20000,0,0,0,BT3_RUMBLE_LOG=1';
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failed++; };

(async () => {
  const b = await launch({ headless: true });
  const ev = (e) => evaluate(b.send, e);
  const get = (k) => ev(`bt3Module.ccall('Bt3Ui_Get','number',['string'],['${k}'])`);
  const lines = () => ev(`window.bt3Out.map((x) => x[1])`);
  await b.send('Page.enable');
  const start = async () => {
    await b.send('Page.navigate', { url: PAGE });
    for (let i = 0; i < 240 && !(await ev(`!!(globalThis.settings && settings.engineReady)`).catch(() => false)); i++) await sleep(250);
    for (let i = 0; i < 120 && !(await lines()).some((l) => l.startsWith('sticks: right')); i++) await sleep(250);  // (the 40th pad read)
    await ev(`settings.refresh(), 0`);
  };
  const stick = async () => (await lines()).find((l) => l.startsWith('sticks: left')) || '';
  const change = (id, value) => ev(`(() => { const i = document.getElementById('${id}'); i.value = ${JSON.stringify(String(value))};
    i.dispatchEvent(new Event('input')); i.dispatchEvent(new Event('change')); return true; })()`);
  const click = (id) => ev(`document.getElementById('${id}').click(), 0`);

  await b.send('Page.navigate', { url: 'http://127.0.0.1:8080/index.html' });
  await ev(`localStorage.removeItem('bt3.settings'), 0`);
  await start();
  // fonts (the list is made on the game's thread and arrives a moment after it is first asked for: web_ui.c)
  const fontList = (shared) => ev(`bt3Module.ccall('Bt3Ui_FontList','string',['number'],[${shared}])`);
  for (let i = 0; i < 40 && !(await fontList(0)).includes('\n'); i++) await sleep(250);
  await ev(`settings.refresh(), 0`);
  const menuFonts = (await fontList(0)).split('\n');
  const textFonts = (await fontList(1)).split('\n');
  check(menuFonts.length >= 2 && menuFonts[0] === '0\tOriginal' && textFonts.length >= 2,
        `font lists: menu ${JSON.stringify(menuFonts.map((l) => l.split('\t')[1]))}, dialogue ${JSON.stringify(textFonts.map((l) => l.split('\t')[1]))}`);
  check(await ev(`document.getElementById('set-menu_font_id').options.length`) === menuFonts.length, 'the panel offers the same menu fonts');
  const menuPick = Number(menuFonts[1].split('\t')[0]), textPick = Number(textFonts[textFonts.length - 1].split('\t')[0]);
  await change('set-menu_font_id', menuPick);
  await change('set-text_font_id', textPick);
  // low input lag
  check(await get('late_input') === 0, 'low input lag: off by default');
  await click('set-late_input');
  // deadzone, as the stick is now (Original)
  const before = await stick();
  check(/deadzone -1 .* game \+0\.[1-9]/.test(before) || /deadzone -1 .* game \+0\.\d*[1-9]/.test(before), `Original: the stick at 61% reaches the game: ${before.slice(7)}`);
  await change('set-deadzone-0', 80);
  // vibration
  check(await ev(`document.getElementById('set-vibration').checked`), 'vibration: on by default');
  await change('set-vibration-strength', 50);
  const n0 = (await lines()).length;
  await ev(`document.getElementById('set-vibration-test').click(), 0`);
  await sleep(500);
  const test = (await lines()).slice(n0).find((l) => l.startsWith('rumble: test')) || '';
  check(/rumble: test, player 1 -> SDL_RumbleGamepad\(low (\d+), high \1, 500 ms\)/.test(test), `Test reaches the motors: ${test}`);
  const half = Number((test.match(/low (\d+)/) || [])[1]);
  await sleep(1500);  // (the engine saves between frames)
  // the next start
  await start();
  // (the menu font is applied once the labels made from the disc are there: menu_text.c labels_take)
  for (let i = 0; i < 40 && !(await get('menu_font_id') === menuPick && await get('text_font_id') === textPick); i++) await sleep(250);
  await fontList(0); await sleep(1000); await ev(`settings.refresh(), 0`);
  check(await get('menu_font_id') === menuPick && await get('text_font_id') === textPick &&
        Number(await ev(`document.getElementById('set-menu_font_id').value`)) === menuPick,
        `after a reload: the fonts kept (menu ${menuPick}, dialogue ${textPick}; engine ${await get('menu_font_id')} / ${await get('text_font_id')}, ` +
        `panel ${await ev(`document.getElementById('set-menu_font_id').value`)}; saved ${JSON.stringify((await ev(`localStorage.getItem('bt3.settings') || ''`)).split('\n').filter((l) => /font/.test(l)))})`);
  check(await get('late_input') === 1 && await ev(`document.getElementById('set-late_input').checked`), 'after a reload: low input lag on (engine and panel)');
  const after = await stick();
  check(/deadzone 80 .* game \+0\.0000 \+0\.0000, direction -/.test(after) && await ev(`document.getElementById('set-deadzone-0').value`) === '80',
        `after a reload: deadzone 80% keeps the 61% stick from the game: ${after.slice(7)}`);
  check(await ev(`document.getElementById('set-vibration-strength').value`) === '50', 'after a reload: vibration strength 50%');
  await change('set-vibration-strength', 100);
  const n1 = (await lines()).length;
  await ev(`document.getElementById('set-vibration-test').click(), 0`);
  await sleep(500);
  const full = Number((((await lines()).slice(n1).find((l) => l.startsWith('rumble: test')) || '').match(/low (\d+)/) || [])[1]);
  check(full > half && half > 0, `the strength reaches the motors: 50% -> ${half}, 100% -> ${full}`);
  await click('set-vibration');
  check(!(await ev(`document.getElementById('set-vibration').checked`)) && await ev(`document.getElementById('set-vibration-strength').disabled`) &&
        await ev(`bt3Module.ccall('Bt3Ui_Vibration','number',['number','number'],[0,0])`) === 0, 'vibration off: engine off, strength and Test unavailable');
  await b.close();
  console.log(failed ? `RESULT: FAIL (${failed})` : 'RESULT: PASS (a controller made by SDL: no physical hardware)');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
