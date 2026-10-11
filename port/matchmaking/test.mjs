// The Worker run against a small stand-in for its database: the address rules and the limits.
// Run: node port/matchmaking/test.mjs   (any recent Node; nothing is sent anywhere)
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
const src = fs.readFileSync(new URL("./worker.js", import.meta.url), "utf8").replace("export default", "const worker =") + "\nexport { worker, network, isPrivate, sameNetwork, shown };";
const copy = path.join(fs.mkdtempSync(path.join(os.tmpdir(), "worker-test-")), "worker.mjs");
fs.writeFileSync(copy, src);
const { worker, network, sameNetwork } = await import(copy);
const rooms = new Map(), used = new Map();
let rowids = 0;
const DB = { prepare(sql) { return { args: [], bind(...a) { this.args = a; return this; },
    async first() { const a = this.args;
        if (sql.startsWith("SELECT COUNT(*) AS n FROM rooms WHERE queue") && sql.includes("rowid <")) { const mine = rooms.get(a[3]); return { n: [...rooms.values()].filter((r) => r.queue === a[0] && r.version === a[1] && r.join_key === null && r.made >= a[2] && mine && r.rowid < mine.rowid).length }; }
        if (sql.startsWith("SELECT COUNT(*) AS n FROM rooms WHERE queue")) { return { n: [...rooms.values()].filter((r) => r.queue === a[0] && r.version === a[1] && r.join_key === null && r.made >= a[2]).length }; }
        if (sql.startsWith("SELECT * FROM rooms WHERE queue")) { const l = [...rooms.values()].filter((r) => r.queue === a[0] && r.version === a[1] && r.join_key === null && r.made >= a[2]).sort((x, y) => x.rowid - y.rowid); return l[0] ?? null; }
        if (sql.startsWith("SELECT COUNT")) { return { n: [...rooms.values()].filter((r) => r.ip === a[0]).length }; }
        if (sql.startsWith("SELECT * FROM rooms")) { const r = rooms.get(a[0]); return r && r.made >= a[1] ? r : null; }
        if (sql.startsWith("SELECT n FROM used")) { const n = used.get(a[0] + "|" + a[1]); return n ? { n } : null; }
        if (sql.startsWith("INSERT INTO used")) { const k = a[0] + "|" + a[1]; used.set(k, (used.get(k) ?? 0) + 1); return { n: used.get(k) }; }
        throw new Error("first: " + sql); },
    async run() { const a = this.args;
        if (sql.startsWith("DELETE FROM rooms WHERE made")) { for (const [k, r] of rooms) { if (r.made < a[0]) rooms.delete(k); } return {}; }
        if (sql.startsWith("DELETE FROM used")) { return {}; }
        if (sql.startsWith("INSERT INTO used")) { used.set(a[0] + "|" + a[1], a[2]); return {}; }
        if (sql.startsWith("INSERT INTO rooms")) { if (rooms.has(a[0])) throw new Error("taken"); rooms.set(a[0], { code: a[0], made: a[1], version: a[2], ip: a[3], host_key: a[4], join_key: null, host_name: a[5], join_name: null, host_addrs: a[6], join_addrs: null, queue: a[7] ?? null, who: a[8] ?? null, rowid: ++rowids }); return {}; }
        if (sql.startsWith("DELETE FROM rooms WHERE who")) { for (const [k, r] of rooms) { if (r.who === a[0] && r.queue !== null && r.join_key === null) rooms.delete(k); } return {}; }
        if (sql.startsWith("DELETE FROM rooms WHERE code = ? AND join_key IS NULL")) { const r = rooms.get(a[0]); if (r && r.join_key === null) { rooms.delete(a[0]); return { meta: { changes: 1 } }; } return { meta: { changes: 0 } }; }
        if (sql.startsWith("UPDATE rooms SET join_key")) { const r = rooms.get(a[3]); if (!r || r.join_key !== null) return { meta: { changes: 0 } }; r.join_key = a[0]; r.join_name = a[1]; r.join_addrs = a[2]; return { meta: { changes: 1 } }; }
        if (sql.startsWith("UPDATE rooms SET host_addrs")) { rooms.get(a[1]).host_addrs = a[0]; return {}; }
        if (sql.startsWith("DELETE FROM rooms WHERE code")) { rooms.delete(a[0]); return {}; }
        throw new Error("run: " + sql); } }; } };
const env = { DB }; // (no TURN key: no logins are asked of Cloudflare)
async function post(what, ip, body) {
    const r = await worker.fetch(new Request("https://x/v1/" + what, { method: "POST", headers: { "cf-connecting-ip": ip }, body: JSON.stringify(body) }), env);
    return r.json();
}
let fails = 0;
function check(name, ok) { console.log((ok ? "ok   " : "FAIL ") + name); if (!ok) fails++; }
check("IPv6 network part", network("2001:db8:1:2:aaaa::1") === network("2001:db8:1:2::ffff") && network("2001:db8:1:3::1") !== network("2001:db8:1:2::1"));
// strangers: the local address is not handed over, either way
let h = await post("host", "198.51.100.1", { v: 3, name: "H", addrs: ["198.51.100.1:7000", "192.168.1.10:7000"] });
let j = await post("join", "203.0.113.9", { v: 3, code: h.code, name: "J", addrs: ["203.0.113.9:8000", "10.0.0.5:8000"] });
check("stranger joins: host's local address hidden", JSON.stringify(j.peer.addrs) === JSON.stringify(["198.51.100.1:7000"]));
let p = await post("poll", "198.51.100.1", { code: h.code, key: h.key });
check("host is told the stranger's public address and seen address only", JSON.stringify(p.peer.addrs) === JSON.stringify(["203.0.113.9:8000", "seen=203.0.113.9"]));
p = await post("poll", "203.0.113.9", { code: h.code, key: j.key });
check("the stranger's later poll: still hidden", JSON.stringify(p.peer.addrs) === JSON.stringify(["198.51.100.1:7000"]));
// one household, same public address
h = await post("host", "198.51.100.1", { v: 3, name: "H", addrs: ["198.51.100.1:7000", "192.168.1.10:7000"] });
await post("addrs", "198.51.100.1", { code: h.code, key: h.key, addrs: ["198.51.100.1:7000", "192.168.1.10:7000", "relay=104.30.1.1:5000"] });
j = await post("join", "198.51.100.1", { v: 3, code: h.code, name: "J", addrs: ["198.51.100.1:8000", "192.168.1.11:8000"] });
check("same address: local and relay addresses handed over", JSON.stringify(j.peer.addrs) === JSON.stringify(["198.51.100.1:7000", "192.168.1.10:7000", "relay=104.30.1.1:5000"]));
p = await post("poll", "198.51.100.1", { code: h.code, key: h.key });
check("host is told the housemate's local address (and no marker)", JSON.stringify(p.peer.addrs) === JSON.stringify(["198.51.100.1:8000", "192.168.1.11:8000", "seen=198.51.100.1"]));
// one household, one seen by IPv6, one by IPv4, same public IPv4 found by STUN
h = await post("host", "2001:db8:1:2::10", { v: 3, name: "H", addrs: ["198.51.100.1:7000", "192.168.1.10:7000"] });
j = await post("join", "198.51.100.1", { v: 3, code: h.code, name: "J", addrs: ["198.51.100.1:8000", "192.168.1.11:8000"] });
check("IPv6 and IPv4 of one household: local address handed over", j.peer.addrs.includes("192.168.1.10:7000"));
check("sameNetwork: two IPv6 of one network", sameNetwork("2001:db8:1:2::10", [], "2001:db8:1:2:5::1", []));
// wrong codes
let last;
for (let i = 0; i < 31; i++) { last = await post("join", "192.0.2.77", { v: 3, code: "ZZZZZ" + "23456789ABCDEFGHJKMNPQRSTUVWXYZ"[i], name: "X", addrs: ["192.0.2.77:1"] }); }
check("the 31st wrong code in an hour is refused as busy", last.error === "busy");
h = await post("host", "198.51.100.2", { v: 3, name: "H", addrs: ["198.51.100.2:7000"] });
last = await post("join", "192.0.2.77", { v: 3, code: h.code, name: "X", addrs: ["192.0.2.77:1"] });
check("... and so is a right code from that address", last.error === "busy");
// rooms an hour (20), with the room given up each time
for (let i = 0; i < 21; i++) { last = await post("host", "192.0.2.50", { v: 3, name: "H", addrs: ["192.0.2.50:1"] }); if (last.code) await post("leave", "192.0.2.50", { code: last.code, key: last.key }); }
check("the 21st room in an hour from one address is refused", last.error === "busy");
check("no relay without a key says why", typeof h.noRelay === "string");
// the relay's use measured: Cloudflare's analytics and its TURN service stood in for
const cap = { DB, TURN_KEY_ID: "abc123", TURN_KEY_TOKEN: "t", CF_ACCOUNT_ID: "0123abcd", CF_ANALYTICS_TOKEN: "a", ADMIN_KEY: "0123456789abcdef0123" };
let monthBytes = 10e9, analyticsFail = false;
const revoked = [];
const realFetch = globalThis.fetch;
globalThis.fetch = async (u, init) => {
    u = String(u);
    if (u.includes("/graphql")) {
        if (analyticsFail) { return new Response(JSON.stringify({ data: null, errors: [{ message: "not allowed" }] }), { status: 200 }); }
        return new Response(JSON.stringify({ data: { viewer: { accounts: [{ month: [{ sum: { egressBytes: monthBytes / 2, ingressBytes: monthBytes / 2 }, dimensions: { keyId: "abc123" } }],
            logins: [{ sum: { egressBytes: 1.5e9, ingressBytes: 0.5e9 }, dimensions: { username: "heavy" } }, { sum: { egressBytes: 5e7, ingressBytes: 5e7 }, dimensions: { username: "light" } }] }] } } }));
    }
    if (u.endsWith("/revoke")) { revoked.push(u.split("/credentials/")[1].split("/")[0]); return new Response(null, { status: 204 }); }
    if (u.includes("generate-ice-servers")) { return new Response(JSON.stringify({ iceServers: [{ urls: ["turn:turn.example:3478?transport=udp"], username: "u", credential: "c" }] })); }
    return realFetch(u, init);
};
async function postCap(what, ip, body) {
    const r = await worker.fetch(new Request("https://x/v1/" + what, { method: "POST", headers: { "cf-connecting-ip": ip }, body: JSON.stringify(body) }), cap);
    return r.json();
}
h = await postCap("host", "192.0.2.90", { v: 3, name: "H", addrs: ["192.0.2.90:1"] });
check("measuring set up but never done: no relay login", typeof h.noRelay === "string" && h.noRelay.includes("measured"));
check("status needs the admin key", (await postCap("status", "192.0.2.90", { admin: "wrong" })).error === "bad request");
analyticsFail = true;
let st = await postCap("status", "192.0.2.90", { admin: cap.ADMIN_KEY });
check("analytics refusing: reported, nothing measured", st.measured === false && st.why.includes("not allowed"));
analyticsFail = false;
st = await postCap("status", "192.0.2.90", { admin: cap.ADMIN_KEY });
check("under the limit: relay on, 10 GB counted", st.measured === true && st.relay === "on" && st.monthGB === 10);
check("the login that passed 2 GB is ended, the light one is not", JSON.stringify(revoked) === JSON.stringify(["heavy"]));
h = await postCap("host", "192.0.2.91", { v: 3, name: "H", addrs: ["192.0.2.91:1"] });
check("after a measurement: a relay login is given", h.turn && h.turn.username === "u");
st = await postCap("status", "192.0.2.90", { admin: cap.ADMIN_KEY });
check("a login is ended once only", revoked.length === 1);
monthBytes = 600e9;
st = await postCap("status", "192.0.2.90", { admin: cap.ADMIN_KEY });
check("past the month's limit: relay off, every login in use ended", st.relay === "off until next month" && revoked.includes("light"));
h = await postCap("host", "192.0.2.92", { v: 3, name: "H", addrs: ["192.0.2.92:1"] });
check("... and no more logins, rooms still made", typeof h.code === "string" && h.noRelay === "the month's relay allowance is used up");
await worker.scheduled({}, cap, { waitUntil: (p) => p });
check("the Cron Trigger's entry runs", true);
// quick match
rooms.clear();
let q1 = await post("quick", "198.51.100.20", { v: 3, name: "A", addrs: ["198.51.100.20:7000"], queue: "D15", who: "aaaaaaaaaaaa" });
check("quick match, nobody waiting: a room of one's own", q1.role === "host" && typeof q1.code === "string" && typeof q1.key === "string");
let qs = await post("queues", "198.51.100.99", { v: 3 });
check("the queue's count shows the one waiting", qs.waiting.D15 === 1 && qs.waiting.S === 0);
let q2 = await post("quick", "203.0.113.30", { v: 3, name: "B", addrs: ["203.0.113.30:8000", "10.0.0.9:8000"], queue: "S", who: "bbbbbbbbbbbb" });
check("another queue is another queue", q2.role === "host");
let q3 = await post("quick", "203.0.113.31", { v: 3, name: "C", addrs: ["203.0.113.31:8000", "10.0.0.7:8000"], queue: "D15", who: "cccccccccccc" });
check("the same queue: put with the one who waits", q3.role === "join" && q3.code === q1.code && q3.peer.name === "A" && JSON.stringify(q3.peer.addrs) === JSON.stringify(["198.51.100.20:7000"]));
p = await post("poll", "198.51.100.20", { code: q1.code, key: q1.key });
check("... who is told of it by its poll, without the stranger's local address", p.peer && p.peer.name === "C" && JSON.stringify(p.peer.addrs) === JSON.stringify(["203.0.113.31:8000", "seen=203.0.113.31"]));
let q4 = await post("quick", "203.0.113.32", { v: 3, name: "D", addrs: ["203.0.113.32:8000"], queue: "D15", who: "dddddddddddd" });
check("a room that has its two is not given again", q4.role === "host" && q4.code !== q1.code);
let q5 = await post("quick", "203.0.113.32", { v: 3, name: "D", addrs: ["203.0.113.32:8000"], queue: "D15", who: "dddddddddddd" });
check("searching again: not put with one's own room left behind", q5.role === "host" && !rooms.has(q4.code));
let q6 = await post("quick", "203.0.113.33", { v: 4, name: "E", addrs: ["203.0.113.33:8000"], queue: "D15", who: "eeeeeeeeeeee" });
check("another netcode version is not matched", q6.role === "host");
check("an unknown queue is refused", (await post("quick", "203.0.113.34", { v: 3, name: "F", addrs: ["203.0.113.34:1"], queue: "X", who: "ffffffffffff" })).error === "bad request");
check("no name for the game: refused", (await post("quick", "203.0.113.34", { v: 3, name: "F", addrs: ["203.0.113.34:1"], queue: "S" })).error === "bad request");
// two who search in the same moment: both made a room before either looked again
rooms.clear();
{
    const first = await post("quick", "198.51.100.40", { v: 3, name: "G", addrs: ["198.51.100.40:1"], queue: "T", who: "gggggggggggg" });
    // the second one's first look finds nobody (as it would have, a moment earlier): stood in for by hiding the room
    const kept = rooms.get(first.code); rooms.delete(first.code);
    const realPrepare = DB.prepare;
    let put = false;
    DB.prepare = (sql) => { const st = realPrepare(sql); if (!put && sql.startsWith("INSERT INTO rooms")) { put = true; rooms.set(first.code, kept); } return st; };
    const second = await post("quick", "203.0.113.41", { v: 3, name: "H", addrs: ["203.0.113.41:1"], queue: "T", who: "hhhhhhhhhhhh" });
    DB.prepare = realPrepare;
    check("two at once: the later one gives its room up and joins the earlier", second.role === "join" && second.code === first.code && rooms.size === 1);
}
console.log(fails === 0 ? "all passed" : fails + " failed");
process.exit(fails === 0 ? 0 : 1);
