// The signalling Worker against SQLite (local.mjs's stand-in for D1): rooms, rounds of offer and answer, the keys,
// the limits, CORS. Nothing is sent anywhere (no TURN key: no request to Cloudflare).
//   node port/web/signal/test.mjs
import { d1 } from "./local.mjs";
const { default: worker } = await import(new URL("worker.js", import.meta.url));

let env = { DB: d1() }, fails = 0, clock = 1_800_000_000;
Date.now = () => clock * 1000; // (the Worker's clock: moved by the test)
const check = (name, ok) => { console.log((ok ? "ok   " : "FAIL ") + name); if (!ok) fails++; };
async function call(what, ip, body, origin = "http://127.0.0.1:8080") {
    const headers = { "cf-connecting-ip": ip, "content-type": "application/json", origin };
    const r = await worker.fetch(new Request("https://x/v1/" + what, { method: "POST", headers, body: JSON.stringify(body) }), env);
    const j = r.headers.get("content-type") === "application/json" ? await r.json() : null;
    return { status: r.status, cors: r.headers.get("access-control-allow-origin"), ...j };
}
const OFFER = "v=0 offer", ANSWER = "v=0 answer";

// a room, joined; the lobby's round
let h = await call("host", "198.51.100.1", { v: 4, name: "Goku" });
check("host: a code of 6, a key, STUN only without a TURN key", /^[A-Z2-9]{6}$/.test(h.code) && h.key.length === 24 && h.ice.length === 1 && h.ice[0].urls.startsWith("stun:"));
check("CORS: the page's origin is answered (ALLOWED_ORIGINS unset: any)", h.cors === "http://127.0.0.1:8080");
let p = await call("poll", "198.51.100.1", { code: h.code, key: h.key });
check("host polls: nobody yet, no answer", p.peer === null && p.answer === null && p.offer === null);
check("offer, round 1", (await call("offer", "198.51.100.1", { code: h.code, key: h.key, round: 1, sdp: OFFER })).status === 200);
let j = await call("join", "203.0.113.9", { v: 4, code: h.code.toLowerCase(), name: "Vegeta" });
check("join (code in small letters): a key, the host's name", j.key && j.key !== h.key && j.peer.name === "Goku" && Array.isArray(j.ice));
p = await call("poll", "203.0.113.9", { code: h.code, key: j.key });
check("joiner polls: the offer of round 1, no answer shown to it", p.offer && p.offer.round === 1 && p.offer.sdp === OFFER && p.answer === null && p.peer.name === "Goku");
check("joiner cannot offer", (await call("offer", "203.0.113.9", { code: h.code, key: j.key, round: 2, sdp: OFFER })).status === 400);
check("host cannot answer", (await call("answer", "198.51.100.1", { code: h.code, key: h.key, round: 1, sdp: ANSWER })).status === 400);
check("an answer to another round is refused", (await call("answer", "203.0.113.9", { code: h.code, key: j.key, round: 2, sdp: ANSWER })).status === 400);
check("answer, round 1", (await call("answer", "203.0.113.9", { code: h.code, key: j.key, round: 1, sdp: ANSWER })).status === 200);
p = await call("poll", "198.51.100.1", { code: h.code, key: h.key });
check("host polls: the joiner's name and the answer of round 1, no offer shown to it", p.peer.name === "Vegeta" && p.answer.round === 1 && p.answer.sdp === ANSWER && p.offer === null);
// the session's round (both pages loaded again)
await call("offer", "198.51.100.1", { code: h.code, key: h.key, round: 2, sdp: OFFER + "2" });
p = await call("poll", "198.51.100.1", { code: h.code, key: h.key });
check("a new offer clears the old answer", p.answer === null);
p = await call("poll", "203.0.113.9", { code: h.code, key: j.key });
check("joiner sees round 2", p.offer.round === 2 && p.offer.sdp === OFFER + "2");
await call("answer", "203.0.113.9", { code: h.code, key: j.key, round: 2, sdp: ANSWER + "2" });
p = await call("poll", "198.51.100.1", { code: h.code, key: h.key });
check("host gets round 2's answer", p.answer.round === 2 && p.answer.sdp === ANSWER + "2");
// refusals
check("a third player: full", (await call("join", "192.0.2.5", { v: 4, code: h.code, name: "Gohan" })).error === "full");
check("a wrong key: no room", (await call("poll", "192.0.2.5", { code: h.code, key: "X".repeat(24) })).error === "no room");
const h2 = await call("host", "198.51.100.1", { v: 4, name: "Goku" });
check("another release: version", (await call("join", "203.0.113.9", { v: 3, code: h2.code, name: "Old" })).error === "version");
check("an offer too large is refused", (await call("offer", "198.51.100.1", { code: h2.code, key: h2.key, round: 1, sdp: "x".repeat(20000) })).status === 400);
check("not JSON: bad request", (await worker.fetch(new Request("https://x/v1/host", { method: "POST", body: "{" }), env)).status === 400);
// leaving, and rooms going away
check("leave", (await call("leave", "203.0.113.9", { code: h.code, key: j.key })).status === 200);
check("the room is gone after leave", (await call("poll", "198.51.100.1", { code: h.code, key: h.key })).error === "no room");
clock += 601;
check("a room unused for ten minutes is gone", (await call("poll", "198.51.100.1", { code: h2.code, key: h2.key })).error === "no room");
const h3 = await call("host", "198.51.100.1", { v: 4, name: "A" });
clock += 500; await call("poll", "198.51.100.1", { code: h3.code, key: h3.key }); clock += 500;
check("a room in use stays (each poll keeps it)", (await call("poll", "198.51.100.1", { code: h3.code, key: h3.key })).status === 200);
// limits
let busy = 0;
for (let i = 0; i < 6; i++) if ((await call("host", "192.0.2.77", { v: 4, name: "x" })).error === "busy") busy++;
check("at most 4 open rooms per address", busy === 2);
let wrong = 0;
for (let i = 0; i < 32; i++) wrong += (await call("join", "192.0.2.88", { v: 4, code: "AAAAAA", name: "x" })).error === "busy" ? 1 : 0;
check("codes are not to be found by trying (30 wrong codes an hour)", wrong === 2);
// CORS
env = { DB: d1(), ALLOWED_ORIGINS: "https://bt3.example, http://127.0.0.1:8080" };
check("ALLOWED_ORIGINS: a listed origin is answered", (await call("host", "198.51.100.1", { v: 4, name: "x" }, "https://bt3.example")).cors === "https://bt3.example");
check("ALLOWED_ORIGINS: another origin is refused", (await call("host", "198.51.100.1", { v: 4, name: "x" }, "https://evil.example")).status === 403);
const pre = await worker.fetch(new Request("https://x/v1/host", { method: "OPTIONS", headers: { origin: "https://bt3.example" } }), env);
check("preflight: 204 with the allowed headers", pre.status === 204 && pre.headers.get("access-control-allow-headers") === "content-type");
console.log(fails ? `RESULT: FAIL (${fails})` : "RESULT: PASS");
process.exit(fails ? 1 : 0);
