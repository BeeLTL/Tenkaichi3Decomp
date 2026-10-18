// The signalling Worker (worker.js) run on this machine, for tests and for play on one network without Cloudflare:
// the same code, with Node's SQLite in place of D1 (schema.sql) and the caller's address in place of Cloudflare's.
//   node port/web/signal/local.mjs [port, default 8787] [database file, default: in memory]
// The page is told where it is with ?signal=http://<this machine>:<port> (kept by the page, net_web.js). The
// variables TURN_KEY_ID / TURN_KEY_TOKEN / ALLOWED_ORIGINS / TURN_DAILY_MAX are taken from the environment, as the
// Worker's are from Cloudflare.
import fs from "node:fs";
import http from "node:http";
import { DatabaseSync } from "node:sqlite";

const here = new URL(".", import.meta.url);
const { default: worker } = await import(new URL("worker.js", here));

// D1's calls (prepare / bind / first / run / all), as worker.js uses them, over SQLite
export function d1(file = ":memory:") {
    const db = new DatabaseSync(file);
    db.exec(fs.readFileSync(new URL("schema.sql", here), "utf8"));
    return {
        prepare(sql) {
            const st = db.prepare(sql);
            let args = [];
            return {
                bind(...a) { args = a; return this; },
                async first() { return st.get(...args) ?? null; },
                async run() { const r = st.run(...args); return { meta: { changes: Number(r.changes) } }; },
                async all() { return { results: st.all(...args) }; },
            };
        },
    };
}

export function serve(port, file) {
    const env = { DB: d1(file) };
    for (const k of ["TURN_KEY_ID", "TURN_KEY_TOKEN", "ALLOWED_ORIGINS", "TURN_DAILY_MAX"]) {
        if (process.env[k]) env[k] = process.env[k];
    }
    const server = http.createServer(async (req, res) => {
        const chunks = [];
        for await (const c of req) chunks.push(c);
        const headers = new Headers();
        for (const [k, v] of Object.entries(req.headers)) if (typeof v === "string") headers.set(k, v);
        headers.set("cf-connecting-ip", (req.socket.remoteAddress ?? "").replace(/^::ffff:/, ""));
        const body = req.method === "GET" || req.method === "HEAD" || req.method === "OPTIONS" ? undefined : Buffer.concat(chunks);
        let r;
        try {
            r = await worker.fetch(new Request(`http://${req.headers.host ?? "localhost"}${req.url}`, { method: req.method, headers, body }), env);
        } catch (e) {
            r = new Response(JSON.stringify({ error: "internal" }), { status: 500 });
            console.error(e);
        }
        res.writeHead(r.status, Object.fromEntries(r.headers));
        res.end(Buffer.from(await r.arrayBuffer()));
    });
    return new Promise((done) => server.listen(port, () => done(server)));
}

if (import.meta.url === `file://${process.argv[1].replace(/\\/g, "/").replace(/^(?=[A-Za-z]:)/, "/")}`) {
    const port = Number(process.argv[2] || 8787);
    await serve(port, process.argv[3]);
    console.log(`signalling on http://127.0.0.1:${port} (worker.js, SQLite ${process.argv[3] || "in memory"})`);
}
