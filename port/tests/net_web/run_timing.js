// P0 timing probe runner (docs/BROWSER_ONLINE_PLAN.md): build port/tests/net_web/timing_probe.c with
//   emcc timing_probe.c -O2 -sJSPI=1 -sUSE_SDL=3 -o <dir>/probe.html, serve <dir> on 127.0.0.1:8095 (port/web/devserver.py --root <dir> --port 8095),
//   then: node port/tests/net_web/run_timing.js <repo> [mdns]   ('mdns' keeps Chrome's default hiding of local addresses)
const path = require('path');
const { launch, evaluate, sleep } = require(path.join(process.argv[2], 'port/tests/lib/cdp.js'));
(async () => {
  const extraArgs = process.argv[3] === 'mdns' ? [] : ['--disable-features=WebRtcHideLocalIpsWithMdns'];
  const b = await launch({ headless: true, extraArgs });
  await b.send('Page.navigate', { url: 'http://127.0.0.1:8095/probe.html' });
  for (let i = 0; i < 60; i++) {
    await sleep(1000);
    const r = await evaluate(b.send, `JSON.stringify({ result: (typeof Module !== 'undefined' && Module.result) || null, err: (typeof Module !== 'undefined' && Module.rtcError) || null })`);
    const o = JSON.parse(r);
    if (o.result || o.err) { console.log(o.result || 'rtc error: ' + o.err); break; }
    if (i === 59) console.log('no result after 60 s');
  }
  await b.close();
})().catch((e) => { console.error(e); process.exit(1); });
