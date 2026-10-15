// Signalling for the browser build's online play (docs/BROWSER_ONLINE_PLAN.md, P4): room codes, and the two pages'
// WebRTC offer and answer.
//
// The browser's counterpart of port/matchmaking/worker.js (the native games' service, which hands out "ip:port"
// addresses). A page cannot use UDP addresses: two pages connect with WebRTC, which needs each to read the other's
// session description (an offer, then an answer, with the ICE candidates in them) before any packet flows. This
// Worker passes them on. No game data passes through here; once the two pages are connected they talk directly (or
// through the TURN relay, if no direct way works).
//
// What it stores (a D1 database bound as DB, see schema.sql), per room, for at most ten minutes after its last use:
// the code, the two keys, the two player names, the netcode version, the address of the host as Cloudflare saw it
// (for the limits), and the latest offer and answer with their round number.
//
// Requests are JSON over POST, from a page on any origin listed in ALLOWED_ORIGINS (comma-separated; unset: any):
//   /v1/host   {v, name}                  -> {code, key, ice}
//   /v1/join   {v, code, name}            -> {key, peer: {name}, ice}
//   /v1/offer  {code, key, round, sdp}    -> {}            (the host: its offer for this round)
//   /v1/answer {code, key, round, sdp}    -> {}            (the one who joined: its answer to that round's offer)
//   /v1/poll   {code, key}                -> {peer: {name} | null, offer: {round, sdp} | null, answer: {round, sdp} | null}
//   /v1/leave  {code, key}                -> {}
// A round is one connection: 1 in the lobby, 2 after the pages have loaded themselves again for the session (the
// connection does not survive the reload; the room is what lets them find each other again). The host offers, the
// other answers, each round anew. `ice` is the list of ICE servers for RTCPeerConnection: Cloudflare's STUN server,
// and its TURN relay when this Worker has a TURN key (the secrets TURN_KEY_ID and TURN_KEY_TOKEN), good for three
// hours, within the daily limits below. Errors come back as {error: "..."} with a 4xx status: "version" (the two
// games differ), "no room", "full", "busy", "bad request".

const ROOM_SECONDS = 600;        // a room unused for ten minutes is gone
const ROOMS_PER_ADDRESS = 4;     // rooms one address may have open at a time
const HOSTS_PER_HOUR = 20;       // rooms one address may make in an hour
const WRONG_CODES_PER_HOUR = 30; // codes that name no room, from one address in an hour (codes are not to be found by trying)
const RELAY_PER_DAY_ADDRESS = 20; // relay logins one address may be given in a day (a room takes two)
const RELAY_PER_DAY = 1000;      // relay logins given in a day, all told (the variable TURN_DAILY_MAX overrides it)
const SDP_MAX = 16384;           // the largest offer or answer taken (a few kilobytes with every candidate in it)
const STUN = { urls: "stun:stun.cloudflare.com:3478" };
const ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789"; // no I, L, O, 0, 1: they are misread

function randomText(length) {
    const bytes = new Uint8Array(length);
    crypto.getRandomValues(bytes);
    let out = "";
    for (const b of bytes) {
        out += ALPHABET[b % ALPHABET.length];
    }
    return out;
}

// A name as the game sends it: printable ASCII, at most 16 characters.
function cleanName(name) {
    return String(name ?? "Player").replace(/[^\x20-\x7e]/g, "").slice(0, 16) || "Player";
}

// The CORS headers for this request's origin: null if the origin is not allowed.
function corsHeaders(request, env) {
    const origin = request.headers.get("origin");
    const allowed = String(env.ALLOWED_ORIGINS ?? "").split(",").map((s) => s.trim()).filter(Boolean);
    if (origin !== null && allowed.length > 0 && !allowed.includes(origin)) {
        return null;
    }
    return {
        "access-control-allow-origin": origin ?? "*",
        "access-control-allow-methods": "POST, OPTIONS",
        "access-control-allow-headers": "content-type",
        "access-control-max-age": "86400",
        vary: "origin",
    };
}

function reply(cors, body, status = 200) {
    return new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json", "cache-control": "no-store", ...cors } });
}

// Counts one more use of `what` in the time slot that starts at `slot` and says how many that makes (0: not counted).
async function countUse(env, what, slot) {
    try {
        const row = await env.DB.prepare("INSERT INTO used (what, slot, n) VALUES (?, ?, 1) ON CONFLICT(what, slot) DO UPDATE SET n = n + 1 RETURNING n")
            .bind(what, slot).first();
        return row ? row.n : 0;
    } catch (e) {
        return 0;
    }
}

async function usesSoFar(env, what, slot) {
    try {
        const row = await env.DB.prepare("SELECT n FROM used WHERE what = ? AND slot = ?").bind(what, slot).first();
        return row ? row.n : 0;
    } catch (e) {
        return 0;
    }
}

// The ICE servers for one page: STUN, and the TURN relay if there is a key and the day's limits allow it.
async function iceServers(env, ip, now) {
    if (!env.TURN_KEY_ID || !env.TURN_KEY_TOKEN) {
        return [STUN];
    }
    const day = now - (now % 86400);
    const all = await countUse(env, "relay", day);
    if (all === 0 || all > (Number(env.TURN_DAILY_MAX) || RELAY_PER_DAY) || (await countUse(env, "relay:" + ip, day)) > RELAY_PER_DAY_ADDRESS) {
        return [STUN]; // (not counted, or over a limit: a direct connection only)
    }
    try {
        const r = await fetch(`https://rtc.live.cloudflare.com/v1/turn/keys/${env.TURN_KEY_ID}/credentials/generate-ice-servers`, {
            method: "POST",
            headers: { authorization: `Bearer ${env.TURN_KEY_TOKEN}`, "content-type": "application/json" },
            body: JSON.stringify({ ttl: 10800 }),
        });
        if (!r.ok) {
            return [STUN];
        }
        const j = await r.json();
        const list = Array.isArray(j.iceServers) ? j.iceServers : j.iceServers ? [j.iceServers] : [];
        return list.length > 0 ? list : [STUN];
    } catch (e) {
        return [STUN];
    }
}

function cleanSdp(sdp) {
    return typeof sdp === "string" && sdp.length > 0 && sdp.length <= SDP_MAX ? sdp : null;
}

export default {
    async fetch(request, env) {
        const url = new URL(request.url);
        const cors = corsHeaders(request, env);
        if (cors === null) {
            return new Response("origin not allowed\n", { status: 403 });
        }
        if (request.method === "OPTIONS") {
            return new Response(null, { status: 204, headers: cors });
        }
        if (request.method === "GET" && url.pathname === "/") {
            return new Response("Tenkaichi3Decomp browser signalling: room codes. See the repository, port/web/signal.\n", { headers: cors });
        }
        if (request.method !== "POST" || !url.pathname.startsWith("/v1/")) {
            return reply(cors, { error: "bad request" }, 404);
        }
        let body;
        try {
            body = await request.json();
        } catch (e) {
            return reply(cors, { error: "bad request" }, 400);
        }
        if (body === null || typeof body !== "object") {
            return reply(cors, { error: "bad request" }, 400);
        }
        const ip = request.headers.get("cf-connecting-ip") ?? "";
        const now = Math.floor(Date.now() / 1000);
        const hour = now - (now % 3600);
        const what = url.pathname.slice(4);

        if (what === "host") {
            if (!Number.isInteger(body.v)) {
                return reply(cors, { error: "bad request" }, 400);
            }
            await env.DB.prepare("DELETE FROM rtc_rooms WHERE used < ?").bind(now - ROOM_SECONDS).run();
            const open = await env.DB.prepare("SELECT COUNT(*) AS n FROM rtc_rooms WHERE ip = ?").bind(ip).first();
            if ((open && open.n >= ROOMS_PER_ADDRESS) || (await countUse(env, "host:" + ip, hour)) > HOSTS_PER_HOUR) {
                return reply(cors, { error: "busy" }, 429);
            }
            if (now % 50 === 0) { // now and then: counts of past days are thrown away
                await env.DB.prepare("DELETE FROM used WHERE slot < ?").bind(now - 2 * 86400).run().catch(() => {});
            }
            const key = randomText(24);
            for (let attempt = 0; attempt < 5; attempt++) {
                const code = randomText(6);
                try {
                    await env.DB.prepare("INSERT INTO rtc_rooms (code, used, version, ip, host_key, host_name) VALUES (?, ?, ?, ?, ?, ?)")
                        .bind(code, now, body.v, ip, key, cleanName(body.name)).run();
                } catch (e) {
                    continue; // the code is taken: another one
                }
                return reply(cors, { code, key, ice: await iceServers(env, ip, now) });
            }
            return reply(cors, { error: "busy" }, 503);
        }

        const code = String(body.code ?? "").toUpperCase().replace(/[^A-Z0-9]/g, "");
        if (what === "join" && (await usesSoFar(env, "miss:" + ip, hour)) >= WRONG_CODES_PER_HOUR) {
            return reply(cors, { error: "busy" }, 429);
        }
        const room = code.length === 6 ? await env.DB.prepare("SELECT * FROM rtc_rooms WHERE code = ? AND used >= ?").bind(code, now - ROOM_SECONDS).first() : null;
        if (!room) {
            if (what === "join") {
                await countUse(env, "miss:" + ip, hour);
            }
            return reply(cors, { error: "no room" }, 404);
        }

        if (what === "join") {
            if (!Number.isInteger(body.v)) {
                return reply(cors, { error: "bad request" }, 400);
            }
            if (body.v !== room.version) {
                return reply(cors, { error: "version" }, 409);
            }
            const key = randomText(24);
            // only if nobody has joined yet (the update tells whether it was this request that got the place)
            const done = await env.DB.prepare("UPDATE rtc_rooms SET join_key = ?, join_name = ?, used = ? WHERE code = ? AND join_key IS NULL")
                .bind(key, cleanName(body.name), now, code).run();
            if (!done.meta || done.meta.changes !== 1) {
                return reply(cors, { error: "full" }, 409);
            }
            return reply(cors, { key, peer: { name: room.host_name }, ice: await iceServers(env, ip, now) });
        }

        const isHost = body.key === room.host_key, isJoin = room.join_key !== null && body.key === room.join_key;
        if (!isHost && !isJoin) {
            return reply(cors, { error: "no room" }, 404);
        }
        if (what === "poll") {
            await env.DB.prepare("UPDATE rtc_rooms SET used = ? WHERE code = ?").bind(now, code).run();
            const peerName = isHost ? room.join_name : room.host_name;
            return reply(cors, {
                peer: peerName !== null ? { name: peerName } : null,
                offer: isJoin && room.offer !== null ? { round: room.offer_round, sdp: room.offer } : null,
                answer: isHost && room.answer !== null ? { round: room.answer_round, sdp: room.answer } : null,
            });
        }
        if (what === "offer" || what === "answer") {
            const sdp = cleanSdp(body.sdp);
            if (sdp === null || !Number.isInteger(body.round) || body.round < 1 || body.round > 100 || (what === "offer") !== isHost) {
                return reply(cors, { error: "bad request" }, 400);
            }
            if (what === "offer") { // a new round: the answer to the old one is of no use any more
                await env.DB.prepare("UPDATE rtc_rooms SET offer = ?, offer_round = ?, answer = NULL, answer_round = NULL, used = ? WHERE code = ?")
                    .bind(sdp, body.round, now, code).run();
            } else {
                if (room.offer_round !== body.round) {
                    return reply(cors, { error: "bad request" }, 400); // (an answer to an offer that is not the current one)
                }
                await env.DB.prepare("UPDATE rtc_rooms SET answer = ?, answer_round = ?, used = ? WHERE code = ?").bind(sdp, body.round, now, code).run();
            }
            return reply(cors, {});
        }
        if (what === "leave") {
            await env.DB.prepare("DELETE FROM rtc_rooms WHERE code = ?").bind(code).run();
            return reply(cors, {});
        }
        return reply(cors, { error: "bad request" }, 404);
    },
};
