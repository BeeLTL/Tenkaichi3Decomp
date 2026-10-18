// The Settings panel's Online tab (settings.js buildOnline, docs/BROWSER_ONLINE_PLAN.md P5): the signalling server's
// address, which room codes go through (net_web.js). Checks: unset says so; a wrong address is refused; a saved one
// reaches bt3Net and is kept for the next visit (local storage); ?signal= sets it too; an empty one clears it.
//   node port/tests/web/signal_setting.js
// Needs the dev server on 127.0.0.1:8080 with the game data.
'use strict';
const { launch, evaluate, sleep } = require('../lib/cdp.js');

const BASE = 'http://127.0.0.1:8080/index.html';
let failed = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failed++; };

(async () => {
  const b = await launch({ headless: true });
  const ev = (e) => evaluate(b.send, e);
  await b.send('Page.enable');
  const open = async (url) => {
    await b.send('Page.navigate', { url });
    for (let i = 0; i < 100 && !(await ev(`!!document.getElementById('signal-url')`).catch(() => false)); i++) await sleep(100);
  };
  const state = () => ev(`document.querySelector('#signal-url').parentElement.querySelector('[role=status]').textContent`);
  const save = (v) => ev(`(() => { const i = document.getElementById('signal-url'); i.value = ${JSON.stringify(v)};
    [...i.parentElement.querySelectorAll('button')].find((x) => x.textContent === 'Save').click(); return true; })()`);
  await open(BASE);
  await ev(`localStorage.removeItem('bt3.signal'), bt3Net.signalUrl = '', 0`);
  await open(BASE);
  check(/Not set/.test(await state()) && (await ev(`bt3Net.signalUrl`)) === '', `unset: "${await state()}"`);
  await save('not an address');
  check(/not a web address/.test(await state()) && (await ev(`bt3Net.signalUrl`)) === '', `a wrong address is refused: "${await state()}"`);
  await save('https://signal.example.workers.dev/');
  check((await ev(`bt3Net.signalUrl`)) === 'https://signal.example.workers.dev' && /go through https:\/\/signal\.example\.workers\.dev/.test(await state()),
        `saved (the trailing / taken off): bt3Net.signalUrl = ${await ev(`bt3Net.signalUrl`)}`);
  await open(BASE);
  check((await ev(`bt3Net.signalUrl`)) === 'https://signal.example.workers.dev' && (await ev(`document.getElementById('signal-url').value`)) === 'https://signal.example.workers.dev',
        'kept for the next visit (the field shows it too)');
  await open(`${BASE}?signal=${encodeURIComponent('http://127.0.0.1:8787')}`);
  check((await ev(`bt3Net.signalUrl`)) === 'http://127.0.0.1:8787' && (await ev(`document.getElementById('signal-url').value`)) === 'http://127.0.0.1:8787',
        '?signal= sets it, and the field shows it');
  await save('');
  await open(BASE);
  check((await ev(`bt3Net.signalUrl`)) === '' && /Not set/.test(await state()), 'an empty address clears it');
  await b.close();
  console.log(failed ? `RESULT: FAIL (${failed})` : 'RESULT: PASS');
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
