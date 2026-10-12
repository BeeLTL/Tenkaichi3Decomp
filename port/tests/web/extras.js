// Test of what the browser version says about content added from outside the disc (port/src/web/web_extras.c and the
// settings panel's Extras tab), in Chrome through the DevTools protocol:
//   node port/tests/web/extras.js
// Checks: with nothing added, the log says once for each of stages, songs and texture packs that they are not available
// in the browser and why, and the panel says the same; with a stage named through BT3_EXTRA_STAGES (no folder needed),
// it is found, counted in the panel, and no "not available" line is written for stages.
'use strict';
const { launch, evaluate, sleep } = require('../lib/cdp.js');

const START_MS = 15000;  // the attract demo is running by then: the game has looked for added content at its start
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failed++; };

(async () => {
  const b = await launch({ headless: true });
  const ev = (e) => evaluate(b.send, e);
  const run = async (env) => {
    await b.send('Page.navigate', { url: `http://127.0.0.1:8080/index.html?autostart=1&env=BT3_DEMO=1${env}` });
    await sleep(START_MS);
    const lines = JSON.parse(await ev(`JSON.stringify(window.bt3Out.map(([, l]) => l).filter((l) => /not available in the browser|stages:|songs:|texture pack/.test(l)))`));
    const get = (n) => ev(`window.bt3Module.ccall('Bt3Ui_Get', 'number', ['string'], ['${n}'])`);
    const state = { checked: await get('added_checked'), stages: await get('added_stage_count'), songs: await get('added_song_count'),
                    textures: await get('texture_pack_count') };
    await ev(`document.getElementById('settings-open').click(); document.getElementById('tab-extras').click(); 0`);
    await sleep(500);
    const panel = await ev(`[...document.querySelectorAll('#tab-extras-page h3')].find((h) => h.textContent.startsWith('Added stages')).nextElementSibling.textContent`);
    return { lines, state, panel };
  };

  // ---- nothing added ----
  let r = await run('');
  check(r.state.checked === 7, `all three looked for (added_checked ${r.state.checked})`);
  check(r.state.stages === 0 && r.state.songs === 0 && r.state.textures === 0, 'nothing found');
  for (const what of ['added stages', 'added songs', 'texture packs']) {
    const n = r.lines.filter((l) => l.startsWith(`bt3: ${what}: not available in the browser version (`)).length;
    check(n === 1, `log: "${what}: not available in the browser version" once, with the reason (${n})`);
  }
  check(/^Not available in the browser version\..*cannot be looked through/.test(r.panel), `panel: ${r.panel}`);

  // ---- a stage named through BT3_EXTRA_STAGES: found, so not reported as unavailable ----
  r = await run(',BT3_EXTRA_STAGES=0x24');
  check(r.state.stages === 1, `BT3_EXTRA_STAGES: 1 added stage found (${r.state.stages})`);
  check(r.lines.some((l) => l.includes('stages: 1 from BT3_EXTRA_STAGES')), 'log: the engine\'s own line for it');
  check(!r.lines.some((l) => l.startsWith('bt3: added stages: not available')), 'log: no "not available" line for stages');
  check(r.lines.filter((l) => l.includes('not available in the browser')).length === 2, 'log: songs and texture packs still reported');
  check(r.panel.startsWith('Found: 1 added stage. Songs and texture packs: not available in the browser version.'), `panel: ${r.panel}`);

  await b.close();
  console.log(failed ? `RESULT: FAIL (${failed})` : 'RESULT: PASS');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
