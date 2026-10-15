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
