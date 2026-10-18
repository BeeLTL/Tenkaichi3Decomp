# Tenkaichi3Decomp — Web

**Dragon Ball Z: Budokai Tenkaichi 3** (PlayStation 2, USA release, SLUS-21678) running in your web browser.

This is the game's own code, from the matching decompilation in **BT3-Decompiled**, compiled to WebAssembly, with
a renderer, sound and input layer for the web underneath. It is not an emulator. It is a fork of
[z3xox/Tenkaichi3Decomp](https://github.com/z3xox/Tenkaichi3Decomp), the native PC port it is built on.

> **Not a website, not the official project.** This is a fork of
> [z3xox/Tenkaichi3Decomp](https://github.com/z3xox/Tenkaichi3Decomp), the official source. The web version runs
> only on your own computer (a local server on 127.0.0.1) with the data from **your own disc**; nobody hosts it.
> Any website that lets you play Budokai Tenkaichi 3 in a browser, with or without ads, is not affiliated with this
> fork or with the original project: a site that runs the game without your own disc is handing out the game's
> copyrighted code and data. Releases here are free, with no ads, accounts or payments.

This repository contains **no game data**. You need your own disc image of that release. The page reads the
game's data from a folder on your own computer, through a small server that listens on 127.0.0.1 only: nothing
is uploaded anywhere.

## What works

- The whole game: boot, opening movie, menus, fights, two players, saving.
- Two renderers that draw the same picture: **WebGL 2** (the default) and **WebGPU** (`?gpu=webgpu`).
- Widescreen, the internal resolution, and the PS2's effects (outline, glow, depth tint, distance blur), each
  switchable.
- Music, voices and sound effects.
- **Keyboard and controllers**, fully rebindable. The settings show a picture of a controller: click any button
  on it to change its key or controller button. Guided set-up, keyboard presets, and a test that lights the
  buttons on the picture.
- A notification when a controller connects or disconnects, also while playing.
- **Touch controls** for phones and tablets: d-pad, analog stick, face and shoulder buttons, START and SELECT,
  with size, opacity, left or right hand, and a layout editor.
- **Saves** kept in the browser, with a backup, and export and import as a file.
- **Online play, browser against browser**: the game's own *Tenkaichi Budokai Online* menu (Create Room, Join Room
  by a six-letter code, Copy Code), with chat (Tab) and a connection meter. See [Playing online](#playing-online).
- Per-stick deadzone and controller vibration (Settings, Controls), "Low input lag", and the PC version's menu,
  battle and dialogue fonts.
- Languages: the game's language screen and Settings, Video, Language (English is included; other languages need
  their catalogs: [docs/port/translations.md](docs/port/translations.md)).
- Texture packs made for PCSX2, in the data folder's `textures` folder; stages and songs added in the data folder's
  `stages` and `songs` folders, with their names in the menus.
- Side bars beside the picture in full screen, and a meter (pictures a second, game speed).
- "Unlock everything" (a debug function the game's developers left in).
- A performance view and a diagnostics export (Settings, Developer).

**Not in the browser version:** the Vulkan renderer; rollback online (the browser plays online with input delay:
each side waits for the other's buttons); Quick Match (room codes only); playing online against the PC version; the
PC version's screen reader.

**Tested with:** Chrome and Edge on Windows 11. Firefox and Safari are not tested yet. Controllers and touch were
tested with simulated devices; please report how real ones behave. Online play was tested with two browser windows
on one computer, not yet between two computers over the internet: please report how it goes.
Details: [docs/BROWSER_FEATURE_MATRIX.md](docs/BROWSER_FEATURE_MATRIX.md).

## Playing (the ready-made package)

Download `web-<version>.zip` from the [releases](https://github.com/BeeLTL/Tenkaichi3Decomp/releases) and unzip
it. You need a current **Chrome or Edge**, **[Python 3](https://www.python.org/downloads/)** (on Windows, tick
"Add python.exe to PATH") and **[7-Zip](https://www.7-zip.org/)**, plus your own disc image of the USA release
(SLUS-21678) and about 4 GB of free space. In the unzipped folder:

1. **Unpack your disc** once (a few minutes):

   ```
   python extract_disc.py <your disc image> gamedata
   ```

2. **Play:**

   ```
   python play.py
   ```

   It starts a small local server (127.0.0.1 only) and opens the game in your browser. Keep the window open
   while playing; Ctrl+C stops it. If another browser opens, copy the address it prints into Chrome or Edge.

The package holds only program code: the page reads the game's data from your `gamedata` folder. More in
[docs/BROWSER_RELEASE.md](docs/BROWSER_RELEASE.md).

**Keep the extracted data to yourself:** never put it on a public server or share it.

## Playing online

Two players, each with the web version running on their own computer (with their own disc's data), play against
each other. Online play is in the source code now and comes with the next release package (the current one,
web-0.1.33.3, is older); until then, [build from source](#building-from-source). The browsers connect directly to each other (WebRTC); no game data and no part of the game passes
between them, only the buttons pressed, a check that both games agree, and chat lines.

**What it needs: a signalling server.** Before two browsers can connect, they have to swap a few lines of
connection details. A small server does that for them, under a six-letter room code; once they are connected it is
no longer used. Nobody runs one for you: both players use the same one, set up once.

- **On one network** (two computers at home): one of you runs it on their computer, from the source code, with
  [Node.js](https://nodejs.org/) 22.13 or newer:

  ```
  node port/web/signal/local.mjs 8787
  ```

  Its address is then `http://<that computer's address>:8787`.
- **Over the internet:** put it on your own free Cloudflare account (a Worker and a small database). The steps are in
  [port/web/signal/README.md](port/web/signal/README.md). Adding Cloudflare's TURN relay there (optional) lets players
  connect whose networks block direct connections.

**Then, each player:**

1. Start the game as usual with `python play.py` (it also checks that your data is the unmodified disc's: online
   matches are only between two unmodified games).
2. Open **Settings, Online**, enter the signalling server's address under **Signalling server**, and press **Save**.
   The page keeps it.
3. In the game: **Main Menu, Tenkaichi Budokai Online.**
   - One player: **Create Room**, choose the rules, **Open Room**. The room code is shown; **Copy Code** (Cross)
     puts it on the clipboard. Send it to the other player.
   - The other player: **Join Room**, type the code (or paste it with Ctrl+V), **Join**.
4. Both pages load again for the match and connect again by themselves; the match starts at the character select.
   **Tab** opens the chat; **Settings, Video, Meter** shows the ping and the waiting.

Both players need the same version of this web build. The signalling server sees the room code, the player names
and the connection details (network addresses) for at most ten minutes; see
[port/web/signal/README.md](port/web/signal/README.md).

## Building from source

You need **Emscripten 6.0.12** and the pinned tools in [docs/BROWSER_BUILD.md](docs/BROWSER_BUILD.md), Python 3
and 7-Zip.

1. Unpack your disc: `python port/tools/extract_disc.py <your disc image> gamedata`
2. Build the page (into `port/build/web/`):

   ```
   python port/tools/wasm_build.py game
   python port/tools/wasm_build.py port
   python port/tools/wasm_build.py data
   python port/tools/wasm_build.py link
   ```

3. Play: `python port/web/play.py <path to gamedata>`. To make a package like the release:
   `python port/tools/browser_package.py <name>`.

## Controls

- **F1**, or **START + SELECT** on a controller, opens the settings. **Esc** closes them.
- Keyboard by default: arrows for the d-pad, **K L J I** for Cross, Circle, Square, Triangle, **W A S D** for
  the left stick, **Enter** for START. Everything can be changed in Settings, Controls.
- **Full screen** button at the top right of the page.
- On a phone or tablet the touch controls appear by themselves (Settings, Touch).

## Documents

| Document | What it covers |
|---|---|
| [BROWSER_BUILD.md](docs/BROWSER_BUILD.md) | Toolchain, build steps, every test |
| [BROWSER_RELEASE.md](docs/BROWSER_RELEASE.md) | Running a release package |
| [BROWSER_FEATURE_MATRIX.md](docs/BROWSER_FEATURE_MATRIX.md) | What works, and how it was verified |
| [BROWSER_ARCHITECTURE.md](docs/BROWSER_ARCHITECTURE.md) | How the browser build is put together |
| [BROWSER_ONLINE_PLAN.md](docs/BROWSER_ONLINE_PLAN.md) | Online play in the browser: the design, each step and its test results |
| [port/web/signal/README.md](port/web/signal/README.md) | The signalling server for online play: running and deploying it |
| [BROWSER_ONLY_AUDIT.md](docs/BROWSER_ONLY_AUDIT.md) | Dependencies and their licences |

## Layout

| Path | Contents |
|---|---|
| `port/web/` | The page: settings, controllers, controller picture, touch controls, saves, notifications, online connection, chat and meter |
| `port/web/signal/` | The signalling server for online play (a Cloudflare Worker, also runnable with Node) |
| `port/src/web/` | The engine's browser side: files over HTTP, settings, saves, touch input, the online transport |
| `port/tests/web/` | The browser tests (headless Chrome) |
| `port/tools/` | The build (`wasm_build.py`), disc unpacking, packaging |
| `port/src/` | Shared with the PC version: memory, files, memory card, movies; `gs/` renderer, sound, input |
| `src/`, `include/` | The game's code, from BT3-Decompiled; changes for PC and web are inside `#ifdef PORT` |
| `port/data/` | The game's built-in data tables as shape only (labels, sizes, pointer slots), no values |
| `port/third_party/` | newlib's maths library, xxHash, Dear ImGui (PC only) |

The PC version's code (Vulkan, the setup program, its online play) is still in this repository and builds as before.
For PC releases, see the original project:
[z3xox/Tenkaichi3Decomp](https://github.com/z3xox/Tenkaichi3Decomp).

This project was made with the help of AI.

## Thanks

This port stands on other people's work:

- **BT3-Decompiled**, and everyone who worked on the matching decompilation the game's code comes from.
- **[z3xox](https://github.com/z3xox)**, for Tenkaichi3Decomp, the native PC port this web version is built on,
  and **[RexxColder](https://github.com/RexxColder)** for the OpenGL renderer and the added-content work there.
- **[Emscripten](https://emscripten.org/)** (MIT / UIUC), which compiles the game for the web.
- **[SDL3](https://www.libsdl.org/)** (zlib): window, input, controllers and sound.
- **[Dawn / emdawnwebgpu](https://dawn.googlesource.com/dawn)** (BSD-3-Clause), for the WebGPU renderer.
- **[naga](https://github.com/gfx-rs/wgpu/tree/trunk/naga)** (MIT / Apache-2.0), which turns the shaders into WGSL.
- **[LLVM / Clang and libclang](https://llvm.org/)** (Apache-2.0 with LLVM exception).
- **[newlib](https://sourceware.org/newlib/)**'s maths library (BSD-style) and
  **[xxHash](https://github.com/Cyan4973/xxHash)** (BSD-2-Clause).
- **[Dear ImGui](https://github.com/ocornut/imgui)** (MIT): the PC version's settings window.
- **[Python](https://www.python.org/)**, **[Node.js](https://nodejs.org/)**, **[Chromium](https://www.chromium.org/)**
  and **[7-Zip](https://www.7-zip.org/)**, for the tools, the tests and unpacking the disc.
- The original game's developers, Spike, and its publishers. This project exists out of love for their game.

## Licence

The port's own code (everything under `port/` except `port/third_party/`, the build tools, and the PC and web
changes inside `#ifdef PORT`) is under the [GNU Affero General Public License, version 3](LICENSE) (AGPL-3.0-only),
as the original project is in every commit after its `06e4671` (2026-10-14). Up to and including that commit it was
under the [MIT licence](LICENSE-MIT), and that code, as it was then, stays available under MIT (the release
web-0.1.33.3 is from then). Whoever passes the program on, changed or not, or runs a changed version for others over
a network (a web page included), has to offer its source under the same licence. The libraries in
`port/third_party/` keep their own licences (newlib's maths library: its BSD-style terms, in that folder; xxHash:
BSD-2-Clause; Dear ImGui: MIT). The game's code in `src/` and `include/` comes from the decompilation and is not
ours to license: it belongs to the game's rights holders.

## Legal

This project is not affiliated with or endorsed by the game's developers, publishers or rights holders. It exists
for preservation, study and interoperability, and distributes none of the game's assets. All rights to the game
belong to their owners. You need your own legally obtained copy of the game to use it.
