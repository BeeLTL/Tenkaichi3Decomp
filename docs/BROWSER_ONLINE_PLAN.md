# Browser online play: plan

Status: **plan only, nothing implemented.** Written 2026-10-15 against upstream 0.1.35 (`port/src/gs/net*.c`,
`state.c`, `port/matchmaking`). Every statement about the existing code below was read in it; everything about the
browser side is a design to be proven by the tests named in each phase.

## 1. Summary

Online play in the browser reuses upstream's match code (`net.c`: the session, the input exchange, the input delay,
the sync check, the chat) and replaces only what a browser cannot do: send raw UDP, restart the program, and sign
requests with a secret. The first goal is **browser against browser, without rollback** (input delay only), which is
how upstream's own 32-bit Linux build plays. Browser against PC and rollback in the browser are later, separate
questions (section 7).

## 2. What upstream's online play is made of

| Part | Lines (0.1.35) | What it does | Browser |
|---|---|---|---|
| `net.c` | 1,833 | Greeting, configuration, input packets (the last 16 blanks each), lockstep wait, input delay, sync check, rollback driver, chat lines, session start | **Reused.** All traffic goes through `net_send` / `net_recv` (the only `sendto` / `recvfrom` calls) plus the socket set-up in `net_start` |
| `net_match.c` | 956 | Room codes and quick match through the matchmaking Worker over HTTPS (WinHTTP, libcurl or the curl program, on threads) | **Rewritten transport, same interface** (`Match_*`): browser `fetch`, no threads |
| `net_relay.c` | 603 | A TURN client (RFC 5766 over UDP) for players whose routers block direct packets | **Not needed:** WebRTC has ICE with TURN built in, and can use the same Cloudflare TURN credentials |
| `net_sign.c` | 253 | Signs Worker requests with the release's build key (HMAC-SHA256) | **Not usable:** a key in a web page is public. See section 5 |
| `net_chat.c` | small | Chat lines (text over the same packets) | Reused; the page needs its own chat box (ImGui is not in the browser) |
| `state.c` | 999 | Checksums, rollback save / restore, a session without restarting | Checksums: yes. Save / restore and in-place sessions: **x86-64 and macOS only** (they save the game thread's stack and registers); the browser gets the stubs (`Port_RollCan` = 0, `Port_SessionCan` = 0) |
| Lobby window (`ui.cpp`, "Dragon Net Battle") | ~35 calls | Host / join / code / quick match, delay and rollback choice, names, rules | **New page UI** calling the same `Port_Lobby*` functions |

Facts the plan rests on:
- **Without rollback `net.c` already works:** `sRollMax = !Port_RollCan() ? 0 : ...` (net.c), so the browser plays
  lockstep with input delay, as the 32-bit Linux build does ("The 32-bit Linux build has no rollback", README).
- **The game runs the same in the browser as natively:** the attract demo's fight trace is identical
  (`8ad198bf58dbed33`) on WebGL 2, WebGPU and the native builds. Online play needs this on every frame of a match
  (section 6, test O2).
- **A session restarts the program** where `state.c` cannot reset the game in place (`relaunch()`: `execv` /
  `CreateProcess` with the match in `BT3_NET_*` variables). The browser has no in-place reset either (section 4.3).

## 3. Decisions needed before building

| # | Question | Options | Recommendation |
|---|---|---|---|
| D1 | Which matchmaking service? | (a) upstream's Worker, if z3xox agrees (it can require signed releases: `REQUIRE_KEY`); (b) **our own copy** of `port/matchmaking` (Cloudflare Workers + D1, free tier), extended for WebRTC | **(b)**, and ask z3xox in pull request #78 whether they would rather host it. A browser build cannot sign requests, and sending unsigned traffic to their service without asking is not acceptable |
| D2 | First goal | browser–browser lockstep; or wait for rollback | **browser–browser lockstep** (section 7 says why rollback is a research question) |
| D3 | Session start | (a) recreate the game module in the same page, keeping the WebRTC connection; (b) reload the page and connect again | **(a)**, with (b) as fallback (section 4.3) |
| D4 | Cross-play with the PC | later; needs WebRTC in the PC build (a library such as libdatachannel) or a bridge server | **Later**, upstream's decision |

## 4. Design

### 4.1 Transport: `net_io`, one seam in `net.c`

A small layer with the three things `net.c` does with its socket: open (bind / connect), send to the peer, receive
one packet (non-blocking). Natively it is the current code moved, unchanged in behaviour (UDP, the relay, the
statistics); in the browser it is a WebRTC data channel. `net.c` keeps its packet formats, its `NET_VERSION`, its
timing and its waits.

- Browser: `RTCDataChannel` with `ordered: false, maxRetransmits: 0` (packets like UDP: late or lost, never queued
  behind each other). `net.c` already sends every input 16 times over (`REDUNDANT`), so lost packets are covered.
- Incoming messages are queued in JavaScript by the channel's `onmessage`; `net_recv` takes one (`EM_JS`), returning 0
  when there is none, as `recvfrom` on a non-blocking socket does.
- Addresses: `net.c` compares `sockaddr_in` values. The browser has one peer per session: a fixed placeholder address
  stands for it.
- This seam is the one change in a file shared with the PC builds; it is proposed upstream as a refactor with no
  behaviour change, checked with `port/tests/native/compare.sh` and upstream's own net tests (`BT3_NET_SCRIPT`,
  `BT3_NET_LOSS`, `BT3_NET_LAG`).

**As built (P1).** `port/src/gs/net_io.h` / `net_io.c`, six calls:

| Call | Native (`net_io.c`) | Was in `net.c` |
|---|---|---|
| `NetIo_Open()` | `WSAStartup` (Windows, every time, never cleaned up, as before) + `socket(AF_INET, SOCK_DGRAM)` + non-blocking (`ioctlsocket` / `fcntl`); the result is not checked, as before | `net_start`, `Port_LobbyStart` |
| `NetIo_Bind(h, local)` | `bind` | `net_start` (host; the joiner's room-code port), `Port_LobbyStart` (host) |
| `NetIo_LocalAddr(h, out)` | `getsockname` | `Port_LobbyStartCode` (the port the system chose) |
| `NetIo_SendTo(h, buf, n, to)` | `sendto` | `net_send` (direct packets; the relay path is unchanged) |
| `NetIo_RecvFrom(h, buf, size, from)` | `recvfrom` on the non-blocking socket | `net_recv` (the relay's unwrapping is unchanged) |
| `NetIo_Close(h)` | `closesocket` / `close` | six places (session begin / end, lobby start / cancel / launch) |

Everything else in `net.c` is as it was: packet formats, `NET_VERSION`, the 16-fold input redundancy, the input delay,
the sync check, rollback, the relay calls, the statistics, the waits, the address parsing (`inet_pton`,
`gethostbyname`). `net_io.c` is not in the browser build (`WASM_SKIP`); the browser's own comes with P2.

What the contract does not cover, on purpose (native only, to be met differently in the browser):
- **The socket is shared.** The handle is the socket: `net_match.c` uses it for STUN (its public address) and, on its
  own thread, to ask the TURN server for a relay address (`Match_SocketBusy`), and `net_relay.c` sends TURN messages on
  it. They must speak from the same local port as the match, or NAT traversal fails, so they keep the raw socket. In
  the browser, WebRTC's ICE does STUN and TURN itself: `net_match.c` and `net_relay.c` are not built; `net.c`'s calls
  to them need browser counterparts (the relay ones: "no relay", the matchmaking ones: P4).
- **Addresses.** `net.c` names peers by `sockaddr_in` and compares them. The browser has one peer per session; a
  fixed placeholder address will stand for it (P2).
- **Receive.** Natively "nothing there" is `recvfrom` returning -1 (would block); `net.c` treats any `n <= 0` as
  nothing, so a browser that returns 0 is the same to it.

### 4.2 Signalling and NAT: the Worker, extended

WebRTC needs the two pages to swap an offer and an answer (SDP, with ICE candidates) before packets flow. The
Worker already pairs two players by room code and hands each the other's addresses; it gets two more fields per
room: the host's offer and the guest's answer, kept as long as the room (ten minutes), readable only with the
room's `host_key` / `join_key`. TURN credentials come from the Worker as now (Cloudflare TURN), given to
`RTCPeerConnection` as an ICE server. Needs CORS on the Worker for the page's origin (`http://127.0.0.1:<port>`).

### 4.3 Starting a session without restarting a program

The PC build starts a match by starting itself again with `BT3_NET_*` set. In the page:
- (a) The game module is destroyed and created again in the same page with the match's variables (the page already
  passes variables with `?env=`), while the `RTCPeerConnection` lives in the page's own JavaScript and survives.
  `relaunch()` gets a browser branch that asks the page to do this. To be proven: that the module can be recreated
  without leaking WebGL / WebGPU contexts, audio, or memory (test O3).
- (b) Fallback: the page reloads with the match in its address, and both sides connect again with the same room code.
  Slower (the start-up and a second signalling round) but needs nothing new from the module.

The session's own saves (`net_session/`) live in the module's memory file system, separate from the player's saves.

### 4.4 Timing

`net.c` waits for the other side's input with short sleeps (`SDL_Delay(1)`, `SDL_DelayNS(200000)`). In the browser
the game's waits suspend through JSPI and the page's event loop delivers the data channel's messages in between.
To be measured (test O1): how long such a sleep really takes in the page (timers can be clamped to several
milliseconds), and whether lockstep keeps 30 game ticks a second at an input delay of 2–4 blanks.

### 4.5 The page

A "Play online" window on the page, using the same calls as the PC lobby (`Port_LobbyStartCode`, `Port_LobbyPoll`,
`Port_LobbyCancel`, `Port_NetOptions`, `Port_NetNameSet`, `Port_NetRulesSet`): host with a code, join with a code,
input delay, rules, names; the connection meter (`Port_NetStats`); a chat box. Online play stays hidden unless the
page is served by `play.py` / the dev server (it needs the Worker's address, set in the page's settings).

## 5. What must not happen

- No game data over the network: only inputs, checksums, chat lines and the session's settings (as now).
- No browser traffic to upstream's matchmaking service without z3xox's agreement (D1).
- No secret in the page: the browser build sends no build key; our Worker must not rely on one for the browser.
- The native builds' behaviour does not change: the `net_io` seam is checked with `compare.sh` and upstream's net
  tests before it is proposed.

## 6. Phases and tests

| Phase | Work | Done when (test) |
|---|---|---|
| P0 | Decisions D1–D3; measure sleeps and lockstep timing in the page with a loopback stub | **O1:** a wait of 1 ms in the page measured; lockstep loopback keeps 30 ticks/s at delay 2 |
| P1 | `net_io` seam in `net.c`, native behaviour unchanged | native `compare.sh` identical; upstream's scripted net run (`BT3_NET_SCRIPT`, loss and lag) gives the same results before and after |
| P2 | Browser transport (WebRTC); signalling by hand (two tabs on one machine, offer / answer passed by the test) | **O2:** two headless pages play a scripted match (`BT3_NET_SCRIPT`); their per-blank state checksums (`BT3_HASH`) are identical on every blank; with 10% loss and 100 ms lag too |
| P3 | Session start in the page (4.3 a, else b) | **O3:** host and join from the title screen reach the versus menu on both pages; ten sessions in a row with no growth in memory or contexts |
| P4 | Our matchmaking Worker (copy of `port/matchmaking`, offer / answer fields, CORS), room codes | **O4:** two pages on two machines on different networks meet by room code, directly and through TURN |
| P5 | The page's online window, connection meter, chat box | page test: host / join / cancel / errors shown; the meter shows ping and waits |
| P6 | Real matches; release notes | two people play full matches at home; any desync reported by the sync check (`sync_*`) is logged with the blank |

Rough size: P0–P3 a few days of work; P4–P5 a few more; P6 needs two people and two networks.

## 6a. Results

### P0 (2026-10-15): timing in the page: passed

A probe built like the game (Emscripten 6.0.12, `-sJSPI=1`, SDL3, `-O2`): `net.c`'s kind of waits, then a lockstep
loop at 30 ticks a second over a real `RTCPeerConnection` data channel (`ordered: false, maxRetransmits: 0`) whose
other end, a second connection in the same page, echoes every message. Headless Chrome 154, Windows 11.

| Measured | Result |
|---|---|
| `emscripten_has_asyncify()` | 2 (JSPI): SDL's delays call `emscripten_sleep`, so the game's waits suspend and the page's events (data channel messages) are delivered meanwhile |
| `SDL_Delay(1)`, `SDL_DelayNS(200000)`, `SDL_Delay(0)` | **4.7–5.0 ms** each on average (min 2–4, max 7): the browser's timer clamp. `net.c` asks for 0.2–1 ms |
| Data channel open (offer / answer in the page) | 10–22 ms |
| Lockstep, delay 2, 300 ticks | **30.00 ticks/s**; 0–1 waits for the other side (at most 5 ms) |
| Round trip, measured where the message arrives | median 0.38–0.43 ms, p95 0.73–0.84, max 5.9–12.8 (loopback, one machine) |
| Chrome's default (local addresses hidden behind mDNS names) | works the same (open in 22 ms) |

Conclusions:
- Lockstep at 30 ticks a second fits: a wait costs about 5 ms of a 33 ms tick.
- The 5 ms granularity adds jitter to every wait. The browser `net_io` should wait with a promise that the data
  channel's `onmessage` resolves (with the timeout as a fallback), not with timers, so the game resumes when the
  packet arrives. To be built and measured in P2.
- Not measured yet: two separate pages, real networks, loss. Those are P2 (O2) and P4 (O4).

### P1 (2026-10-15): `net_io`, the transport seam

Commit `657dcec` (and the test harness fixes after it), on top of `ab1e6e7`. Diff: `net.c` 15 lines in, 36 out, all
of them the socket calls listed in 4.1 and the `#ifdef`s around them; no game code (`src/`) changed.

**Upstream's scripted online scenarios, natively, before (`ab1e6e7`) against after (`657dcec`):**
`port/tests/native/net_compare.sh` (new; `net_in_container.sh`, `net_script.py`): the Linux x86-64 program built in
the release container, two copies over UDP on 127.0.0.1, 120 s per scenario, a state checksum every blank. Upstream's
own input recording ("session6") is not in the repository; `net_script.py` makes a fixed made-up table instead.

| Scenario | Blanks (before / after) | Host = joiner, whole state | Before = after, fight values |
|---|---|---|---|
| lockstep, delay 2 | 77,312 / 81,204 | every blank, both builds | no fight reached (menus) |
| 20% loss, delay 2 | 76,609 / 79,800 | every blank, both builds | no fight reached |
| 30% loss, 4 ms lag, delay 3 | 20,604 / 20,809 | every blank, both builds | no fight reached |
| rollback 6, 50 ms latency, 15% loss | 19,404 / 19,404 | every blank, both builds | no fight reached |
| attract demo, lockstep | 4,428 / 4,428 | every blank, both builds | **identical, 4,242 blanks of fighting** |
| attract demo, rollback 6, 50 ms, 15% loss | 4,423 / 4,426 | every blank, both builds | **identical, 4,237 / 4,240 blanks of fighting** |
| attract demo, offline (control) | 4,428 / 4,428 | (one copy) | identical, 4,242 |

`RESULT: PASS` (75 checks); no copy reported a desync. Also: the networked demo fight's values equal the offline
fight's at every blank, lockstep and rollback, in both builds.

The whole-state checksum differs between the two builds at every blank, **also in the offline control**, where
`net.c` never runs: the state holds addresses of functions and variables, which move when any code changes size
(state.c says so). That is why builds are compared on the fight's own values, as upstream's tests do, and copies of
one build on everything.

The first run of the comparison reported 4 failures, each at the last blank of a run: the time-out had cut the
program off in the middle of writing that line. The harness now ignores a last line without its newline.

**Native, offline (`port/tests/native/compare.sh ab1e6e7 657dcec`, `RUN_WINDOWS=1`):** headless trace 70 lines
identical; OpenGL (llvmpipe) trace 12 lines and 4 screenshots identical (frame 450 reached); Vulkan (lavapipe) trace
67 lines and 15 screenshots identical (frame 2100 reached); the Windows build run on this machine: trace 70 lines
identical.

**Other builds:** the 32-bit Linux variant (`gcc -m32`) is not in the release container (no 32-bit headers); in a
throwaway container with `gcc-multilib`, `net.c` compiles before and after with the same 8 warnings and `net_io.c`
with none. Compiled, not run. The macOS variant (`mac64`) was **not built or tested** (no Mac here).

**P0 timing test again (after P1):** 30.00 ticks/s, no waits, round trip median 0.50 to 0.52 ms, sleeps 4.7 to 4.8 ms:
as in P0 (the probe does not use `net_io`; this only shows the machine and the toolchain still behave the same).

**Browser:** the build is byte-identical to before (`bt3.wasm` and `bt3.js` checksums unchanged: `net.c` and
`net_io.c` are not in it); demo fight trace `8ad198bf58dbed33` on WebGL 2 and WebGPU, 48 of 48 screenshots (demo and
boot, both renderers) identical to the 0.1.35 results.

### P2 (2026-10-15): `net.c` in the browser over WebRTC

**As built.**
- `net.c` is now in the browser build; `port/src/web/net_off.c` (the "no match" stand-in) is gone: `net.c` provides
  every function it had.
- `port/src/web/net_web.c`: the browser `net_io` (4.1) over the page's data channel, plus what `net.c` needs of the two
  native-only files: the relay ("no relay": WebRTC's ICE does it) and matchmaking (room codes say "not available in the
  browser version yet", P4).
- `port/web/net_web.js` (`bt3Net`): one `RTCPeerConnection`, one data channel (`ordered: false, maxRetransmits: 0`),
  complete offers / answers (no trickling), a receive queue. Signalling by hand: `offer()`, `answer(offer)`,
  `accept(answer)`.
- Starting a session by restarting the program (`relaunch()`) is not possible in a page: `execv` fails and `net.c`
  says "could not start the program again". Unchanged in P2; P3. Rollback stays off (`Port_RollCan` = 0).
- **The state checksum in the browser** (needed by test O2): `state.c` read the scratchpad, which the browser build keeps
  outside its memory (a crash at the first blank with `BT3_HASH`), and had no list of the game's variables. Now:
  `wasm_build.py` links with a map (`-Wl,-Map`), `port/tools/wasm_state.py` writes `bt3.mem` from it by
  `make_state.py`'s own rule (12 ranges, 577,405 bytes), the page puts it at `/bt3.mem`, and `state.c` has two browser
  branches (the list's place, no scratchpad). The native builds compile the same code (`#ifdef __EMSCRIPTEN__` only).

**Test O2** (`port/tests/web/net_o2.js`): two headless Chrome instances on this machine, the page served by the dev
server with the user's data, the test doing the signalling; both copies fed the same input table (`net_script.py`),
a state checksum every blank (`BT3_HASH`, the whole state: the game's variables and heap). Last run:

| Scenario | Blanks | Rate | Host = joiner, whole state | Desync |
|---|---|---|---|---|
| clean, delay 2 | 4,438 | 59.2 / s | every blank | none |
| 10% loss, 100 ms latency (`BT3_NET_LOSS`, `BT3_NET_LATENCY`), delay 2 | 1,852 | 24.7 / s (lockstep waits out the latency) | every blank | none |
| attract demo fight, delay 2 | 4,428 (4,242 fighting) | 59.0 / s | every blank | none |
| the same fight offline, one page | | | fight values equal to the networked fight at every blank | |

Findings on the way, both about **addresses of the port's own memory** kept in the game's memory, never about game
values:
1. **The C library's allocations.** The game keeps the addresses of blocks the port gives it (file handles). In this
   build they come from the C library's allocator (as in upstream's 32-bit build; the 64-bit builds have an ordered
   region of their own, `PORT_LOW_REGION`), whose layout follows the length of the environment's text:
   `BT3_NET_HOST=1` against `BT3_NET_JOIN=127.0.0.1:1` moved those addresses by 16 bytes (5 bytes of the state differ,
   found with `BT3_HASH_AT`); with settings of equal length every one of the 7,384 pages of the state is the same. The
   test now gives both pages settings of equal length.
2. **Intermittent, unexplained at the time** (explained and fixed since: "P2 recheck" below; not a stack address). In some runs of the clean scenario (not in others: 2 of 3, then
   0 of 2, then 0 of 1), one 4-byte value of `gFileReq` (the game's file request queue, `include/sys/file.h`) differs
   for 2 to 4 blanks at a time, 4 to 24 blanks in a run, after which the two copies are the same again. The values
   (`0x1E1410F0` against `0x1E140BF8`) are addresses in the C stack: the game stored the address of a buffer while
   the two pages were at different stack depths. Every other byte of the state is equal at those blanks, the request's
   own values (id, progress) are equal, the fight is unaffected and `net.c`'s sync check never fired. Not found yet:
   which code stores it (the callers declare `File_Request` with three arguments, the definition has two, so the
   decoding of the entry may be off). **Open before real matches** (P6): to be explained, or the browser given a fixed
   region for such buffers as the 64-bit builds have.

**Regressions:** demo fight trace `8ad198bf58dbed33` on WebGL 2 and WebGPU; 48 of 48 screenshots (demo and boot, both
renderers) identical to 0.1.35; page tests dashboard, controllers, touch, start options, data check, audio, controller
picture, connection notifications, saves, extras: all pass.

**Not done in P2:** a real network or two machines (P4 / P6); the P0 timing improvement (waking the game when a packet
arrives instead of on a 5 ms timer: not needed for 59 blanks a second on one machine, measured again with real
latency in P4); native builds (only `state.c` changed among shared files, inside `#ifdef __EMSCRIPTEN__`; not rebuilt
natively in P2).

### P3 (2026-10-15): starting a session in the page

**Decision D3 changed: (b), the page loads itself again, not (a).** Looked at closer, recreating the game module in a
live page means tearing down a module that is suspended in the middle of a call (JSPI), whose SDL and Emscripten
listeners on the window and document keep its 400 MB alive, and re-attaching the page's own parts (settings, saves,
controllers, dashboard): fragile, and against O3's "no growth over ten sessions". A reload is what `relaunch()` does
natively (a fresh program, `execv`) and frees everything. Its cost: the WebRTC connection does not survive and is made
again after the reload (in P3 by the test; in P4 by the matchmaking service, which keeps the room).

**As built.**
- `net.c` `relaunch()`: one branch, `#elif defined(__EMSCRIPTEN__)`, calls `WebNet_Relaunch()` (the only change to a
  shared file in P3).
- `net_web.c` `WebNet_Relaunch()`: every `BT3_*` variable of the program, as `relaunch()` has just set them (the
  session's `BT3_NET_HOST` / `_JOIN`, `BT3_NET_SESSION`, `BT3_SAVES=net_session`, `BT3_SOUND_TICKS`, `BT3_NOMOVIE`, the
  names, options, rules; or, at the end of a session, without them), to the page; then waits until the page is replaced.
- `net_web.js` `relaunch()`: keeps them in the tab's session storage, closes the connection, reloads. `index.html`: a
  page with such variables takes them in place of its own `BT3_*` ones (as a program started again gets its
  environment) and starts the game at once.
- `web_ui.c`: `Bt3Ui_NetLobbyStart(host)` / `Bt3Ui_NetLobbyPoll()` (net.c's address lobby, the native window's, over the
  connection) and the action `net_launch` (`Port_LobbyLaunch` on the game's own thread, as the native window calls it);
  read-only `net_active`, `net_session`, `net_arrived`.
- `saves.js`: during a session (its card is `net_session/`) nothing is kept: the player's saves are not touched.

**Test O3** (`port/tests/web/net_o3.js`), per cycle: both pages run the game normally, the test connects them; the
lobby greets over the connection (both reach state 2); both launch, both pages load again for the session, the test
connects them again; both reach the character select (`net_arrived`), no desync; the host backs out (Triangle /
Circle held over a few blanks) to the main menu, which ends the session on both (`menu_a.c` `Port_NetLeave`, the other
side told); both pages load again without a session. Each page's memory is measured at the title every cycle.

**Results (2026-10-15):** `RESULT: PASS`, 10 cycles of 10: every one started (both pages relaunched for the session,
connected again), reached the character select on both with no desync, and ended by backing out (both pages relaunched
without a session). Memory at the title, MB, host / join: 571/578, 572/572, 573/573, 574/574, 575/575, 575/575,
576/576, 577/577, 578/578, 579/579, 579/579: within the test's 15%, but rising about 0.8 MB a cycle although every
cycle is a fresh page; probably the browser's own caches rather than the page, **not established**. The sessions did
not write their card in these runs (0 times), so `saves.js`'s new guard was not exercised; the player's saves were not
kept anew in any session page ("saves: kept" never logged there).

After P3: O2 passes again (whole state equal at every blank in all three scenarios this run, 4,464 / 1,852 / 4,428
blanks; the demo fight equals the offline one); demo trace `8ad198bf58dbed33` on both renderers and 48 of 48
screenshots identical to 0.1.35; the ten page tests pass.

Not covered: a real network or two machines; a player relaunched while the other is not (one page reloads, the other
waits: net.c's own time-outs apply, not tested here); sound after a relaunch without a click (the browser may keep it
muted until one: headless Chrome here allows autoplay).

### P2 recheck (2026-10-15): the four-byte difference explained and fixed

**What differed.** Two shapes, both one 4-byte word, every other byte of the state equal:
- *bursts* (clean scenario, some runs): `gFileReq.adxf` (the game's file request queue, `include/sys/file.h`) for the
  2 to 4 blanks a read is in flight, e.g. `0x1E563520` against `0x1E5640C0`; all 32 queue entries equal;
- *persistent* (loss scenario, about 1 run in 2, from blank 1028 on): the first word of a 248-byte game heap block
  allocated at blank 1027 and freed at blank 1299 (`0x1E150C50` against `0x1E156C90`), kept afterwards as stale data
  in free heap space, which the checksum covers.

**Where the value comes from.** Temporary logging (removed again) of every `Port_LowAlloc` and every game heap block,
in both pages: both values are exactly `Port_LowAlloc` block 17 of its page, 140 bytes: a `PortFile`, the handle
`ADXF_Open` returns (`plat_file.c`). Both pages made the same 23 `Port_LowAlloc` requests in the same order and
sizes, and the same game heap allocations (sizes, addresses, blanks); only the addresses `calloc` chose differed. The
game uses the handle opaquely (tests for NULL, passes it back to `ADXF_*`), never computes with it.

**Classification: a process-local pointer, not a divergence of the game.** `calloc`'s layout follows everything else
the page allocated (the environment's text, the renderer's, timing), so the two peers got different addresses for
the same handle. The fight values agreed, the networked demo fight equals the offline one, and `net.c`'s sync check
never fired. **Correction:** P2's finding 2 called these "addresses in the C stack": that came from reading the dump
without its region headers (each region of `BT3_HASH_AT`'s `.dump` is preceded by a line "what address size"); read
correctly they are the port's block addresses. P2's finding 1 (settings of different length) was the same thing.

**Upstream's rule.** Upstream met this natively and solved it in `Port_LowAlloc` for its 64-bit builds
(`PORT_LOW_REGION`): one region at a fixed address, handed out in order, freed blocks reused by size, "so that the
addresses depend only on the order of the requests". Its checksum (`Port_StateHash`) covers the game's variables and
heap, not that region; saved states (rollback) add it (`Port_StateExtra`). Its 32-bit build uses `calloc` and has
the same difference.

**Fix.** The browser build uses upstream's allocator (`plat_mem.c`, `PORT_LOW_ORDERED` = `PORT_LOW_REGION` or the
browser), from a fixed range of linear memory: 64 MB at `0x08000000`, between the PS2's 32 MB and its hardware
registers, which nothing else uses (checked at start like the PS2 regions). The browser's 23 requests take under
1 MB. What the checksum covers is unchanged; nothing is masked or normalised. The browser gets only the allocator:
`Port_GameBigAlloc`, `Port_StateExtra` and `Port_LowRegionSize` stay as they were (64-bit only). Native builds compile
the same code with renamed guards. O2 no longer needs settings of equal length and is back to the natural
`BT3_NET_HOST=1` / `BT3_NET_JOIN=127.0.0.1:1`.

Also fixed on the way: `port/tools/wasm_sig.py` reused an earlier repair (`.sig.ll`) of an object as the starting
point of the next one, which kept a stale signature (found when a stub was replaced by the real function); repairs
now always start from the unrepaired IR and are measured against the object as compiled (`<name>.o.orig`).

**Results after the fix** (two headless Chrome pages on one machine, scripted input; the checksum is the whole state:
the game's variables and heap, every blank):

| Runs | Blanks each | Rate | Host = joiner, whole state | Desync |
|---|---|---|---|---|
| clean, delay 2, 5 runs + 1 in the full O2 | 4,411 | 58.8 / s | every blank, all 6 runs | none |
| 10% loss, 100 ms latency, delay 2, 5 runs + 1 | 1,852 | 24.7 / s | every blank, all 6 runs (before: about 1 in 2 differed) | none |
| attract demo fight, delay 2 (full O2) | 4,428 (4,242 fighting) | 59.0 / s | every blank | none |
| the same fight offline | | | fight values equal at every blank | |

"Whole state" is the game's variables and heap, byte for byte; it is not the whole of each process's memory (the
port's block region, the renderer, the page and the C library's heap are not compared, natively either).

Browser regressions: demo trace `8ad198bf58dbed33` and 12 of 12 screenshots identical to 0.1.35.2 on WebGL 2 and on
WebGPU; boot 12 of 12 on both; page tests dashboard, controllers, touch, start options, saves, extras, audio,
controller picture, connection notifications: pass; O3 10 of 10 cycles (one earlier O3 run failed, at the page's
data check after a reload, while the native check's Docker image was being built on the same machine; run alone it
passed).

Native (`port/tests/native/compare.sh`, Docker release image, Linux x86-64; the Windows builds compiled, not run):
pre-P2 `da638d3` against P2 `481f12c` (`state.c`), P2 against P3 `e32a829` (`net.c`), and `3cdb91f` against the fix
`c107e78` (`plat_mem.c`): all three pairs identical: the headless demo trace (70 lines), the OpenGL trace and 4
screenshots, the Vulkan trace and 15 screenshots. **Not run:** the native network scenarios (`net_compare.sh`) after
P1; started, then stopped on request before any result. P1's own run (`da638d3`) is the last native network result.

Not covered: a real network or two machines (P4 / P6).

## 7. Later, and why

- **Rollback in the browser.** `state.c` restores the game thread's stack and registers (`ucontext` / `setjmp` and a
  stack switch). WebAssembly keeps part of its call stack where the program cannot read or write it, so this cannot
  be ported as it is. Possible routes, each a research project: Emscripten's Asyncify (it can save and restore the
  call stack, at a cost in size and speed to measure on the game's 222,000 lines), or snapshots only at a point where
  the game's call stack is always the same (if the game has one). Until then the browser plays with input delay,
  like upstream's 32-bit Linux build.
- **Browser against PC.** The PC speaks UDP, the browser WebRTC. Either the PC build learns WebRTC data channels (a
  library such as libdatachannel, upstream's decision), or a bridge server translates (it would carry every match's
  packets). Both sides would also need the same `NET_VERSION` and the same game behaviour frame for frame (test O2
  between a native and a browser copy, once a common transport exists).
- **Quick match** for browser players: after room codes work, with our Worker's queues.
