# Tenkaichi3Decomp in the browser: release web-0.1.37

*Dragon Ball Z: Budokai Tenkaichi 3* (PlayStation 2, USA release, SLUS-21678) in your web browser: the decompiled game's code compiled to WebAssembly, running in a web page on your own computer.
- Based on upstream Tenkaichi3Decomp **0.1.37**. Source: [BeeLTL/Tenkaichi3Decomp](https://github.com/BeeLTL/Tenkaichi3Decomp).
- Tested in Chrome on Windows 11, from the downloaded package itself: unpacking a disc with its `extract_disc.py`, `play.py`, the gameplay check of the data, a demo fight on WebGL 2 and WebGPU (the same as the PC version's), and online play between two browser windows on one computer through the game's menus (room code, typed and pasted, the match, chat, the meter), with its own `signal/local.mjs`. Not tested: two computers over the internet; real controllers and phones (simulated ones only).
- **New since web-0.1.33.3:** online play, browser against browser (see [Playing online](#playing-online)); upstream 0.1.34 to 0.1.37 (Tenkaichi Budokai Online, translations and the language screen, battle fonts, the Solar Flare's whiteout, fixes); settings for menu, battle and dialogue fonts, language, low input lag, stick deadzone and vibration; side bars in full screen, a meter, the names of added stages and songs in the menus.

> **The game's data is not included.** You need your own copy of the game's disc. The package contains only program code; the game's data tables are built empty and filled from your disc when the game starts. Do not share the extracted data, and do not put it on a public server.

## What you need

| | |
|---|---|
| A current **Chrome or Edge** | The page needs JavaScript Promise Integration (JSPI) and WebGL 2, and says so at start if one is missing. Tested: Chrome 154 on Windows 11. Firefox and Safari: **not tested** |
| **Python 3** | [python.org](https://www.python.org/downloads/); on Windows tick "Add python.exe to PATH" when installing |
| **7-Zip** | [7-zip.org](https://www.7-zip.org/), to read the disc image (found where its installer puts it, or on the PATH) |
| Your own disc image | The unmodified USA release (SLUS-21678), as an `.iso`; about 4 GB of free space for the unpacked data |
| For the WebGPU renderer (optional) | A browser with WebGPU enabled: open the page with `?gpu=webgpu` |

## Setting up (the package)

Unzip the package, open a terminal in its folder, then:

1. **Unpack your disc** once, into the `gamedata` folder next to `play.py` (about 4 GB, 68,611 files; a few minutes):

   ```bash
   python extract_disc.py <your disc image> gamedata
   ```

2. **Play:**

   ```bash
   python play.py
   ```

   It checks the data folder, starts a local server (127.0.0.1 only) and opens the game in your browser. Keep the window open while playing; Ctrl+C stops it. With the data elsewhere: `python play.py <data folder>`.

3. If the browser that opens is not Chrome or Edge, copy the address it prints (`http://127.0.0.1:8080/index.html`) into Chrome or Edge.

In the game, **F1** (or START + SELECT on a controller) opens the settings. Keyboard by default: arrows for the d-pad, K L J I for Cross, Circle, Square, Triangle, W A S D for the left stick, Enter for START.

**Package contents:** `web/` (the page and the program, with the fonts, the English text catalogs and the side bars' art), `play.py`, `devserver.py` (the local server, which also checks your data for online play), `make_fingerprint.py`, `extract_disc.py`, `signal/` (online play's signalling server), this file, and `licenses/`.

**Your own server instead:** `python devserver.py --port 8080 --root web --mount gamedata=<data folder>`. Another server must support HTTP Range requests (the game reads its files in byte ranges); without folder listings, added stages, songs and texture packs are reported as unavailable.

## Playing online

Two players, each with this package and their own disc's data, play against each other: the browsers connect
directly (WebRTC). Only the buttons, a check that both games agree, and chat lines pass between them.

1. **A signalling server**, which both players use (once the browsers are connected it is no longer needed):
   - **On one network:** one of you runs it, with [Node.js](https://nodejs.org/) 22.13 or newer, in the package's
     folder: `node signal/local.mjs 8787`. Its address is `http://<that computer's address>:8787`.
   - **Over the internet:** put it on a free Cloudflare account (`signal/README.md`), optionally with Cloudflare's TURN
     relay for networks that block direct connections.
2. **Each player:** `python play.py` as usual (it also checks that your data is the unmodified disc's: online matches
   are only between two unmodified games), then **Settings, Online, Signalling server**: the address, **Save**.
3. **In the game:** Main Menu, **Tenkaichi Budokai Online**. One player: **Create Room**, **Open Room**, **Copy Code**
   (Cross), and sends the code. The other: **Join Room**, types or pastes (Ctrl+V) the code, **Join**. Both pages load
   again for the match and connect by themselves. **Tab** opens the chat; **Settings, Video, Meter** shows the ping.

Both players need this same version. Input delay only (each side waits for the other's buttons), no rollback yet.

## What works

- **Game:** boot, intro movie, menus, battles, two players. The game's own simulation runs exactly as in the PC version.
- **Two renderers:** WebGL 2 (the default) and WebGPU (open the page with `?gpu=webgpu`); both draw the same picture.
- **Settings** (F1, or START + SELECT on a controller), a window over the game: resolution, aspect ratio, HUD layout, menus in 4:3, effects, audio levels, controls (with each stick's deadzone and vibration), game speed, low input lag, fonts and language, side bars, the meter, skip logos / intro / copyright page, Unlock everything.
- **Controllers:** a picture of the controller to change any button's key or controller button, guided set-up that lights the button to press, keyboard presets, a live view and a test. A notification when a controller connects or disconnects.
- **Touch controls:** an on-screen d-pad, analog stick and buttons over the game, played as player 1, shown on touch screens. Size, opacity, left- or right-handed layouts, and a layout editor.
- **Saves:** kept in the browser, with a backup version, and export and import as a `.zip`.
- **Texture packs, added stages and songs:** in the data folder's `textures`, `stages` and `songs` folders; the names of added stages and songs show in the menus.
- **Online play** browser against browser: [Playing online](#playing-online).
- **Performance view** and a diagnostics export (Settings, Developer).

## Known limitations

- **Online play:** tested with two browser windows on one computer, **not yet between two computers** over the internet: please report how it goes. No rollback (input delay), no Quick Match, not against the PC version. **Not in the browser version:** the Vulkan renderer, the screen reader.
- **Smooth motion** is off with WebGL 2, and up to 60 pictures a second with WebGPU.
- **Saves** live in the browser's storage: clearing the site's data, or a private window, loses them. Export a copy.
- **Not tested yet:** real controllers and real phones or tablets (simulated ones are tested), Firefox and Safari, playing an added song. The sound is checked to come out, not how it sounds.

Full list of what was tested: [BROWSER_FEATURE_MATRIX.md](https://github.com/BeeLTL/Tenkaichi3Decomp/blob/main/docs/BROWSER_FEATURE_MATRIX.md).

## Building from source

See [BROWSER_BUILD.md](https://github.com/BeeLTL/Tenkaichi3Decomp/blob/main/docs/BROWSER_BUILD.md): the toolchain (Emscripten 6.0.12), the four build steps and the tests.

## Licence

The project's code is under the GNU AGPL v3 (`licenses/Tenkaichi3Decomp-AGPL.txt`); code from before the original project's switch on 2026-10-14 also stays available under MIT (`licenses/Tenkaichi3Decomp-MIT.txt`). Whoever passes the program on, or runs a changed version for others over a network, has to offer its source. The licences of the libraries in the program (SDL3, Dawn's WebGPU header, Emscripten's runtime, newlib's maths library, xxHash) are in the package's `licenses` folder. The game itself, and its data, belong to their owners and are not part of this release.
