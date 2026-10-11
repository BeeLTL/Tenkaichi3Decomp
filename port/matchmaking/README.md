# Matchmaking Worker (room codes)

A small Cloudflare Worker that lets two players of the port find each other with a short code, without typing
addresses or forwarding a port. It only introduces the two games to each other: no game data passes through it.

Files: `worker.js` (the Worker), `schema.sql` (its one table, as ONE line without comments: the dashboard's console
joins what is pasted into one line, and a `--` comment then takes the rest of the statement with it).

The table `rooms`, one row per room (rows older than ten minutes are removed whenever a room is made):

| Column | |
|---|---|
| `code` | what the host tells the other player: 6 characters |
| `made` | seconds since 1970 |
| `version` | the game's netcode version: both players need the same |
| `ip` | the host's address as Cloudflare saw it (for the limit of rooms per address) |
| `host_key`, `join_key` | secrets handed to each player, so nobody else can read or end the room |
| `host_name`, `join_name` | the players' names |
| `host_addrs`, `join_addrs` | JSON: the addresses each game can be reached at ("ip:port"), best first |

| `queue` | a quick-match room: which queue it waits in (`S`, `T`, `D10`, `D15`, `D20`); empty for a room with a code to hand on |
| `who` | a quick-match room: a random text the host's game made up for this run, so that a room it left behind is not given back to it |

## Quick match (from 0.1.27)

A player who searches a queue is put with whoever waits longest in it, or, if nobody does, gets a room of their own
in that queue and waits there exactly as a host with a room code does (the same `poll`, `addrs`, `leave`; the same
relay rules and ceilings). Five queues: single battle, team battle, DP battle of 10, 15 and 20 DP; the game gives
all of them no time limit. A room lasts ten minutes; the game searches again by itself when it runs out. Limits:
60 searches an hour from one address, and the four open rooms per address that hosting has. One relay login is
asked for per search, not per room: a search that starts again says it holds a relay address already.

**Updating a Worker that is already set up** (in this order, so that nothing is refused in between):

1. D1 -> `bt3-rooms` -> Console. Run these two lines, one at a time:
   `ALTER TABLE rooms ADD COLUMN queue TEXT;`
   `ALTER TABLE rooms ADD COLUMN who TEXT;`
2. The Worker -> Edit code: replace everything with the new `worker.js`, Deploy.

Older releases of the game are not affected: they do not send the new request, and rooms with codes work as before.
Until the Worker is updated, a game that has the Quick match tab says "The matchmaking service does not know quick
match yet." when a search is started.

## Only the newest release (from 0.1.29)

The game tells the service which release it is (`rel`: 129 for 0.1.29). With the variable `MIN_RELEASE` set on the
Worker (Settings -> Variables and Secrets, a plain variable, e.g. `129`), a new room, a join or a quick-match search
from an older game is refused, and so is one from a game that does not say (0.1.28 and earlier). Rooms under way
are not touched. Without the variable, or at 0, everyone is let in.

- Raise it when a release must be the minimum; it takes effect at the next request, with no new code.
- A game of 0.1.29 or later that is refused says so: "This release is too old ... Get the newest release."
  An older one shows its own words for an answer it does not know ("gave no room", "gave no answer").
- It closes the service, not the game: two copies of an old release can still play each other by typing an
  address (Host on a port / Join by address), and the source is open, so a changed copy can claim any number.

## What it keeps

For at most ten minutes per room: the room code, the two player names, the addresses each game reported about
itself, and the host's address as Cloudflare saw it (to limit how many rooms one address can open). Nothing else,
and nothing after the room has expired.

## Setting it up (the Cloudflare dashboard, no tools needed)

1. **Database.** Storage & Databases -> D1 -> Create. Name it `bt3-rooms`. Open its Console, paste the
   contents of `schema.sql`, run it.
2. **Worker.** Workers & Pages -> Create -> Worker ("Hello World"). Give it a name that says nothing about you
   (the name and your account's workers.dev subdomain are part of the public address). Deploy, then Edit code,
   replace everything with the contents of `worker.js`, Deploy.
3. **Binding.** The Worker -> Settings -> Bindings -> Add -> D1 database. Variable name `DB`, database
   `bt3-rooms`. Deploy.
4. Open the Worker's address in a browser: it answers with one line of text.

## The relay (optional)

Some pairs of players cannot reach each other directly (strict routers, mobile connections). With a TURN key the
host of a room also gets an address at Cloudflare's TURN service that passes the match's packets on.

1. Realtime -> TURN Server -> Create. Note the Turn Token ID and the API token (shown once).
2. The Worker -> Settings -> Variables and Secrets: add two **secrets**, `TURN_KEY_ID` and `TURN_KEY_TOKEN`. Deploy.

Without the two secrets the Worker works as before, without a relay. The key never leaves the Worker: each host is
handed a name and password that are good for three hours. Cloudflare's TURN is free for the first 1,000 GB a month
(a match is about 50 MB an hour, and only matches that cannot connect directly use it).

## A ceiling on the relay's use

Cloudflare has no spending limit for its TURN service (a budget alert only sends an e-mail, a day late), and a
relay login lets its holder send as much as they like for three hours. So the Worker measures the relay's use
itself, from Cloudflare's analytics, every five minutes:

- a login that has passed more than 1 GB in three hours is ended at once (a match is about 0.05 GB an hour;
  variable `TURN_LOGIN_GB`);
- when the calendar month's use reaches 500 GB (variable `TURN_MONTHLY_GB`; Cloudflare charges after 1,000),
  every login in use is ended and no more are given until the next month. Rooms go on working, without a relay.

Both directions are counted, which is more than is charged for. The analytics run some minutes behind, so the
ceiling can be passed by what flows in that time. To set it up:

1. Variable `CF_ACCOUNT_ID`: the account's ID (Workers & Pages overview, on the right).
2. Secret `CF_ANALYTICS_TOKEN`: an API token (My Profile -> API Tokens -> Create Token -> custom) with the single
   permission Account -> Account Analytics -> Read.
3. Secret `ADMIN_KEY`: 16 or more characters of your own choosing, for the status check below.
4. Settings -> Trigger Events -> Cron Trigger: `*/5 * * * *`.

Once `CF_ACCOUNT_ID` or `CF_ANALYTICS_TOKEN` is set, no relay login is given unless a measurement has succeeded in
the last two hours: a ceiling that cannot be checked is not relied on. To measure at once and see the result:

    curl -s -X POST $W/v1/status -d '{"admin":"<your ADMIN_KEY>"}'

answers for instance `{"measured":true,"monthGB":0.42,"limitGB":500,"relay":"on","loginsSeen":3,"loginsEnded":0,
"loginsToday":7,...}`, or `"measured":false` and why.

## What a player is told about the other

The name, the public address and port, and for the one who joins the host's relay address. An address of a
private network (192.168.x.x and the like) is handed over only when the two are on one network: seen by the
Worker under the same address (the same network part, for IPv6), or with the same public IPv4 address in what the
games found out themselves.

## Limits against abuse

Anyone can send requests to a Worker, so it counts (table `used`):

- an address may make 20 rooms an hour and have 4 open at a time;
- an address may enter 30 codes an hour that name no room (codes are not to be found by trying);
- an address is given at most 10 relay logins a day, and all addresses together 500 a day (the variable
  `TURN_DAILY_MAX` sets another number). Past that, rooms still work, without a relay. This is the ceiling on what
  the relay can be made to cost: a login lets its holder send through the relay for three hours.

Without the table `used` no relay logins are given at all (rooms work). What the counts do not prevent: many
addresses using up the Worker's own daily requests (100,000 on the free plan; matchmaking then stops until the
next day, at no cost). If a relay key is abused, deleting it (Realtime -> TURN Server) ends every login made from
it at once. A billing notification in the dashboard is worth setting.

## Testing a change

`node port/matchmaking/test.mjs` runs the Worker's code against a small stand-in for its database: who is told
which addresses, and the limits. Nothing is sent anywhere.

## Trying it by hand

    W=https://<worker>.<subdomain>.workers.dev
    curl -s -X POST $W/v1/host -d '{"v":3,"name":"A","addrs":["203.0.113.5:7000"]}'
    # -> {"code":"ABC234","key":"...","you":"<your address>"}
    curl -s -X POST $W/v1/join -d '{"v":3,"code":"ABC234","name":"B","addrs":["198.51.100.9:7000"]}'
    # -> {"key":"...","you":"...","peer":{"name":"A","addrs":["203.0.113.5:7000"]}}
    curl -s -X POST $W/v1/poll -d '{"code":"ABC234","key":"<the host key>"}'
    # -> {"peer":{"name":"B","addrs":["198.51.100.9:7000"]}}

## Limits

The free plan allows 100,000 requests a day. A host asks every two seconds while waiting, so ten minutes of
waiting are 300 requests. A room code is good for ten minutes. One address can have four rooms open.
