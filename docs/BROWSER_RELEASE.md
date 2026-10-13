# Tenkaichi3Decomp in the browser: release web-0.1.33.2

*Dragon Ball Z: Budokai Tenkaichi 3* (PlayStation 2, USA release, SLUS-21678) in your web browser: the decompiled game's code compiled to WebAssembly, running in a web page on your own computer.
- Based on upstream Tenkaichi3Decomp **0.1.33**. Source: [BeeLTL/Tenkaichi3Decomp](https://github.com/BeeLTL/Tenkaichi3Decomp), branch `browser-port`.
- **A preview:** tested in Chrome on Windows 11; controllers and touch with simulated devices only.

> **The game's data is not included.** You need your own copy of the game's disc. The package contains only program code; the game's data tables are built empty and filled from your disc when the game starts. Do not share the extracted data, and do not put it on a public server.

## What you need

| | |
|---|---|
| A current **Chrome or Edge** | The page needs JavaScript Promise Integration (JSPI) and WebGL 2, and says so at start if one is missing. Tested: Chrome 154 on Windows 11. Firefox and Safari: **not tested** |
| **Python 3** | [python.org](https://www.python.org/downloads/); on Windows tick "Add python.exe to PATH" when installing |
| **7-Zip** | [7-zip.org](https://www.7-zip.org/), to read the disc image (`7z` must be on the PATH) |
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

**Package contents:** `web/` (the page and the program), `play.py`, `devserver.py` (the local server), `extract_disc.py`, this file, and `licenses/`.

**Your own server instead:** `python devserver.py --port 8080 --root web --mount gamedata=<data folder>`. Another server must support HTTP Range requests (the game reads its files in byte ranges); without folder listings, added stages, songs and texture packs are reported as unavailable.

## Building from source

```bash
source /c/project/Tenkaichi3Web/env.sh
```

```bash
python port/tools/wasm_build.py game
```

```bash
python port/tools/wasm_build.py port
```

```bash
python port/tools/wasm_build.py data
```

```bash
python port/tools/wasm_build.py link
```

The output goes to `port/build/web/`. The toolchain and tests are in [BROWSER_BUILD.md](BROWSER_BUILD.md).

## Renderers

| Renderer | How | State |
|---|---|---|
| **WebGL 2** | default | the reference: the same picture as the PC port's OpenGL renderer |
| **WebGPU** | `?gpu=webgpu` | complete; identical game trace, its own screenshots per renderer. Smooth motion up to 60 pictures a second |

## What works

- **Game:** boot, intro movie, menus and battles. The game's own simulation gives a trace identical, line for line, to the native PC build's.
- **Settings panel in the page:** resolution, aspect ratio, HUD layout, menus in 4:3, effects, audio levels, controls, game speed, skip logos / intro / copyright page, Unlock everything.
- **Controllers:** a picture of the controller to change any button's key or controller button, guided set-up that lights the button to press, keyboard presets, a live view and a test. A notification when a controller connects or disconnects.
- **Settings** open as a window over the game (F1).
- **Touch controls:** an on-screen d-pad, analog stick and buttons over the game, played as player 1, shown on touch screens. They have size, opacity, left- or right-handed layouts, and a layout editor. Their preferences are kept apart from settings and saves.
- **Saves:** kept in the browser (IndexedDB), with a backup version, recovery, and export and import as a `.zip`.
- **Performance dashboard** and a diagnostics export.
- **Added stages, songs and texture packs:** in the data folder's `stages`, `songs` and `textures` folders, read over HTTP.

## Known limitations

- **Not available in the browser:**
  - online play;
  - the native settings window (ImGui) and the options only it offers: side bars and "Check for updates";
  - the Vulkan renderer.
- **The game-data check is not run** (it would read all 3 GB). The log, the crash report and the dashboard say so.
- **Smooth motion** is off with WebGL 2 (measured: uneven), and up to 60 pictures a second with WebGPU.
- **Not verified in the browser:**
  - playing an added song;
  - physical controllers (simulated ones are tested);
  - touch controls on a real phone or tablet: tested only with simulated touch input on a desktop browser.
  - the sound's quality: the automated test (`port/tests/web/audio.js`) checks that music, effects and voices come out, and that the volume settings reach them, not how they sound.
- **Saves** live in the browser's storage: clearing site data or a private window loses them. Export a copy.
- **Not tested at all:** Firefox and Safari, mobile browsers, and the 32-bit native builds, rendering on a real GPU and the PS2 matching build. The last three are native, outside this release.

## Verification of this release

The detailed results, with the commands used, are in these documents:
- [UPSTREAM_REVIEW_0.1.32_0.1.33.md](UPSTREAM_REVIEW_0.1.32_0.1.33.md): the merges;
- [BROWSER_MIGRATION_PLAN.md](BROWSER_MIGRATION_PLAN.md): M0–M9;
- [BROWSER_FEATURE_MATRIX.md](BROWSER_FEATURE_MATRIX.md): features and tests.

## Licence

The project's code is MIT (`LICENSE`); third-party licences are in [BROWSER_ONLY_AUDIT.md](BROWSER_ONLY_AUDIT.md) §3.1. The game itself, and its data, belong to their owners and are not part of this release.
