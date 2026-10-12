// Test of content added from outside the disc in the browser version: the data folder looked through over HTTP
// (port/src/web/web_fs.c, port/web/bt3_data.js, port/web/devserver.py: <folder>/?list) and what is said about it
// (port/src/web/web_extras.c, the settings panel's Extras tab), in Chrome through the DevTools protocol:
//   node port/tests/web/extras.js --data <your gamedata folder>   (needs a build in port/build/web)
// It starts its own development server (port TEST_PORT) with a temporary folder mounted at /gamedata/test_extras,
// holding a copy of one of the disc's stage models as an added stage and of one of its ADX streams as an added song
// (copied from your own data folder into the system's temporary folder, removed at the end; nothing is published).
// Checks:
//   - listed, nothing added: no "not available" line for stages and songs (as natively: none added); the panel says
//     none; texture packs are said to be not available yet;
//   - listed, one stage and one song added (BT3_STAGES / BT3_SONGS name the test folders): both found, named in the
//     panel, the stage's file reached over HTTP;
//   - a server that does not list folders (its answers to ?list replaced by a page): said for stages and songs, in the
//     log and the panel; a stage named through BT3_EXTRA_STAGES (no folder needed) is still found.
'use strict';
const fs = require('fs'), os = require('os'), path = require('path');
const { spawn } = require('child_process');
const { launch, evaluate, sleep } = require('../lib/cdp.js');

const args = Object.fromEntries(process.argv.slice(2).reduce((acc, a, i, all) => {
  if (a.startsWith('--')) acc.push([a.slice(2), all[i + 1] && !all[i + 1].startsWith('--') ? all[i + 1] : true]);
  return acc;
}, []));
const ROOT = path.resolve(__dirname, '../../..');
const TEST_PORT = 8090;
const START_MS = 15000;      // the attract demo is running by then: the game has looked for added content at its start
const STAGE_SOURCE = 'pzs3us1/00368.bin';      // one of the disc's stage models (plat_stages.c: 368..402)
const SONG_SOURCE = 'disc/DATA/ZS3USOP.ADX';   // one of the disc's ADX streams
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failed++; };

if (!args.data || !fs.existsSync(path.join(args.data, STAGE_SOURCE)) || !fs.existsSync(path.join(args.data, SONG_SOURCE))) {
  console.error(`usage: node port/tests/web/extras.js --data <gamedata folder> (it needs ${STAGE_SOURCE} and ${SONG_SOURCE} there)`);
  process.exit(2);
}

(async () => {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'bt3-extras-'));
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
    const get = (n) => ev(`window.bt3Module.ccall('Bt3Ui_Get', 'number', ['string'], ['${n}'])`);
    const run = async (env) => {
      await b.send('Page.navigate', { url: `http://127.0.0.1:${TEST_PORT}/index.html?autostart=1&env=BT3_DEMO=1${env}` });
      await sleep(START_MS);
      const lines = JSON.parse(await ev(`JSON.stringify(window.bt3Out.map(([, l]) => l).filter((l) => /added stages|added songs|texture packs|stages:|songs:|data: |failed|not implemented|abort/i.test(l)))`));
      const state = { stages: await get('added_stage_count'), songs: await get('added_song_count'), listable: await get('data_listable'),
                      frame: await get('frame') };
      await ev(`document.getElementById('settings-open').click(); document.getElementById('tab-extras').click(); 0`);
      await sleep(500);
      const panel = JSON.parse(await ev(`JSON.stringify([...[...document.querySelectorAll('#tab-extras-page h3')].find((h) => h.textContent.startsWith('Added stages')).parentElement.querySelectorAll('p')].map((p) => p.textContent))`));
      return { lines, state, panel };
    };
    const notAvailable = (lines, what) => lines.filter((l) => l.startsWith(`bt3: ${what}: not available`)).length;

    // ---- listed, nothing added ----
    let r = await run('');
    check(r.state.listable === 1, `the data server lists its folders (${r.state.listable})`);
    check(r.state.stages === 0 && r.state.songs === 0, 'nothing added: none found');
    check(notAvailable(r.lines, 'added stages') === 0 && notAvailable(r.lines, 'added songs') === 0, 'log: nothing said for stages and songs (none added, as natively)');
    check(notAvailable(r.lines, 'texture packs') === 1, 'log: texture packs not available yet, said once');
    check(r.panel[0] === 'Stages: none (the data folder has no stages folder with any).' && r.panel[1].startsWith('Songs: none') &&
          r.panel[2] === 'Texture packs: not available in the browser version yet.', `panel: ${r.panel.join(' | ')}`);

    // ---- listed, a stage and a song added ----
    // (Not covered: a battle on the added stage. BT3_TEST_STAGE=0x24 with the attract demo stops the game at the
    // battle's load, before the stage file is read, also in the build before this listing existed, with no added stage
    // behind the id: BROWSER_ONLY_AUDIT.md, unknowns. Playing an added song needs the menus.)
    r = await run(',BT3_STAGES=gamedata/test_extras/stages,BT3_SONGS=gamedata/test_extras/songs');
    check(r.state.stages === 1 && r.lines.some((l) => l === 'bt3: stages: 1 added from gamedata/test_extras/stages'), 'the added stage is found (log: 1 added)');
    check(r.state.songs === 1 && r.lines.some((l) => l === 'bt3: songs: 1 added from gamedata/test_extras/songs'), 'the added song is found (log: 1 added)');
    const stageAsked = await ev(`performance.getEntriesByType('resource').some((e) => e.name.endsWith('/gamedata/test_extras/stages/Test%20Stage.unk'))`);
    check(stageAsked, 'the added stage\'s file is reached over HTTP (its size, for the stage buffer)');
    check(r.state.frame > 0 && !r.lines.some((l) => /failed|not implemented|abort/i.test(l)), `the game runs on, no read failure or abort (frame ${r.state.frame})`);
    check(r.panel[0] === 'Stages: 1 added stage.' && r.panel[1] === 'Songs: 1 added song.', `panel: ${r.panel.join(' | ')}`);

    // ---- a server that does not list folders ----
    await b.send('Page.enable');
    await b.send('Page.addScriptToEvaluateOnNewDocument', { source: `(() => { const f = window.fetch; window.fetch = (u, o) =>
      String(u).endsWith('?list') ? Promise.resolve(new Response('<html>a folder page</html>', { status: 200, headers: { 'Content-Type': 'text/html' } })) : f(u, o); })();` });
    r = await run(',BT3_EXTRA_STAGES=0x24');
    check(r.state.listable === 0, `listing not available (${r.state.listable})`);
    check(r.lines.some((l) => l.startsWith('data: the data folder cannot be looked through')), 'page log: the data folder cannot be looked through, with the reason');
    check(notAvailable(r.lines, 'added songs') === 1 && r.lines.some((l) => l.includes('the data server does not list its folders')), 'log: songs not available, with the reason');
    check(notAvailable(r.lines, 'added stages') === 0 && r.state.stages === 1, 'a stage named through BT3_EXTRA_STAGES is still found (no "not available" line)');
    check(r.panel[1].startsWith('Songs: not available: the data server does not list its folders'), `panel: ${r.panel.join(' | ')}`);
  } finally {
    await b.close();
    server.kill();
    await sleep(500);
    fs.rmSync(tmp, { recursive: true, force: true });
  }
  console.log(failed ? `RESULT: FAIL (${failed})` : 'RESULT: PASS');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
