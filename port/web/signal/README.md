# Browser signalling: room codes for the browser build

Two copies of the browser build play online over a WebRTC data channel (`port/web/net_web.js`). Before the first
packet, each page must read the other's session description: the host's *offer*, then the joiner's *answer*. This
Worker passes them on, under a six-letter room code. It is the browser's counterpart of `port/matchmaking` (the native
games' service, which hands out UDP addresses instead), and a separate service: the browser build does not talk to
upstream's matchmaking (see `docs/BROWSER_ONLINE_PLAN.md`, decision D1).

The player never sees offers or answers: the game's own room-code lobby (`net.c`) asks `net_web.c`, which asks the page,
which asks this Worker.

## What it stores

The D1 table `rtc_rooms` holds, per room, for at most ten minutes after it was last used:
- the code and two random keys;
- the two player names and the netcode version;
- the host's IP address, used for the limits;
- the latest offer and answer, which include the ICE candidates, that is, the network addresses each browser found
  for itself.

The table `used` counts requests for the limits. No game data passes through.

A room is given up as soon as both pages are connected for the session, or when the game cancels.

## The flow

| Step | Host page | Joining page |
|---|---|---|
| 1 | `/v1/host` gives a code, a key and ICE servers. | |
| 2 | Offer, round 1: `/v1/offer`. | `/v1/join` with the code gives a key and ICE servers. |
| 3 | Polls `/v1/poll` for the answer. | Polls `/v1/poll` for the offer, answers it: `/v1/answer`. |
| 4 | Connected: `net.c`'s lobby greets over the channel, and the players launch. | |
| 5 | The page reloads for the session (P3). Offer, round 2. | The page reloads, then answers round 2. |
| 6 | Connected again: the host gives the room up (`/v1/leave`). | |

`worker.js` documents each request.

## Limits

These are the constants at the top of `worker.js`:
- 4 open rooms per address;
- 20 new rooms per address per hour;
- 30 wrong codes per address per hour;
- offers and answers of at most 16 KB.

Each `/v1/host` and `/v1/join` hands out TURN credentials, valid three hours, but only:
- when the Worker has a TURN key;
- up to 1000 a day overall (the variable `TURN_DAILY_MAX` changes this);
- up to 20 a day per address.

Beyond the limits a page gets STUN only, so a direct connection still works. The relay is used only when no direct
path works, and Cloudflare charges for the traffic it carries, not for the credentials.

## Running it on this machine

```bash
node port/web/signal/local.mjs 8787
```

This runs the same `worker.js` with Node's SQLite in place of D1. Open the page once with
`?signal=http://<this machine>:8787`; the page keeps that address in the browser's local storage. Two machines on
one network can use it this way. Without a TURN key, connections are direct only.

Tests:

```bash
node port/web/signal/test.mjs
```

```bash
node port/tests/web/net_o4.js
```

`test.mjs` covers the Worker itself. `net_o4.js` runs two pages meeting by code; it needs the dev server.

## Deploying it on Cloudflare (your own account)

1. Create the database, then load the schema:
   ```bash
   npx wrangler d1 create bt3-signal
   ```
   ```bash
   npx wrangler d1 execute bt3-signal --remote --file port/web/signal/schema.sql
   ```
2. Write a `wrangler.toml` next to `worker.js`. Use the database ID that step 1 printed:
   ```toml
   name = "bt3-signal"
   main = "worker.js"
   compatibility_date = "2026-10-01"
   [[d1_databases]]
   binding = "DB"
   database_name = "bt3-signal"
   database_id = "<id>"
   [vars]
   ALLOWED_ORIGINS = "https://<where the page is served>"
   ```
3. Optional: give the Worker a TURN relay, from Cloudflare Realtime's TURN service, for players whose networks block
   direct connections:
   ```bash
   npx wrangler secret put TURN_KEY_ID
   ```
   ```bash
   npx wrangler secret put TURN_KEY_TOKEN
   ```
4. Deploy, then open the page once with `?signal=https://bt3-signal.<account>.workers.dev`:
   ```bash
   npx wrangler deploy
   ```

`ALLOWED_ORIGINS` is a comma-separated list of the page origins that may use the Worker. If it is unset, any page may.
