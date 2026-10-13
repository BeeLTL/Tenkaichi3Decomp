# Tenkaichi3Decomp in the browser: release browser-0.1.33.1

This is the browser version of the PC port of *Dragon Ball Z: Budokai Tenkaichi 3* (PlayStation 2, USA release, SLUS-21678): the PC port's code compiled to WebAssembly, running in a web page.
- It is based on upstream Tenkaichi3Decomp **0.1.33**.
- It is a local release: the tag `browser-0.1.33.1` and a package made by `port/tools/browser_package.py`. Nothing has been published.

> **The game's data is not included and must be supplied separately.** You need your own copy of the game's disc. The release contains only program code; the game's data tables are built empty and filled from your disc when the game starts. Do not share the extracted data, and do not put it on a public server.

## What you need

| | |
|---|---|
| A browser with **JavaScript Promise Integration (JSPI)** and **WebGL 2** | A current **Chrome or Edge** (tested: Chrome 154.0.8037.98 on Windows 11; Edge 155 and the app's Chromium 152 ran earlier milestones). Firefox and Safari were **not tested**; the page checks for JSPI at start and says so if it is missing |
| For the WebGPU renderer | A browser with WebGPU enabled and an adapter (tested on Chrome 154, NVIDIA RTX 5070 Ti) |
| Your own disc image of the USA release (SLUS-21678) | Unpacked once with `port/tools/extract_disc.py` (needs `7z`) |
| Python 3 | To run the local server `port/web/devserver.py`, and to unpack the disc |
| To build from source | Emscripten 6.0.12 and the rest of the pinned toolchain: [BROWSER_BUILD.md](BROWSER_BUILD.md) |

## Setting up

1. Unpack your disc into a data folder (about 4 GB, 68,611 files):

   ```bash
   python port/tools/extract_disc.py <your disc image> gamedata
   ```

2. Serve the page and your data folder on your own machine (127.0.0.1 only):

   ```bash
   python port/web/devserver.py --port 8080 --mount gamedata=<path to gamedata>
   ```

   From the release package (its folder: `web/`, `devserver.py`, `extract_disc.py`), run in that folder:

   ```bash
   python devserver.py --port 8080 --root web --mount gamedata=<path to gamedata>
   ```

3. Open `http://127.0.0.1:8080/index.html` in Chrome or Edge.

**Why `devserver.py`:**
- It serves byte ranges, which the game reads its files in.
- It serves folder listings, used for added stages, songs and texture packs.
- It sets the cross-origin isolation headers.

**Another server:** it must support HTTP Range requests. Without listings, added content is reported as unavailable.

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
- **Controllers:** set-up, presets, a live view and a test.
- **Saves:** kept in the browser (IndexedDB), with a backup version, recovery, and export and import as a `.zip`.
- **Performance dashboard** and a diagnostics export.
- **Added stages, songs and texture packs:** in the data folder's `stages`, `songs` and `textures` folders, read over HTTP.

## Known limitations

- **Not available in the browser:**
  - online play;
  - the native settings window (ImGui) and the options only it offers: side bars and "Check for updates". (Skip logos / intro / copyright and menus in 4:3 are in the page's panel since after `browser-0.1.33.1`, commit "Page settings for the start and menu options".)
  - the Vulkan renderer.
- **The game-data check is not run** (it would read all 3 GB). The log, the crash report and the dashboard say so.
- **Smooth motion** is off with WebGL 2 (measured: uneven), and up to 60 pictures a second with WebGPU.
- **Not verified in the browser:**
  - playing an added song;
  - physical controllers (simulated ones are tested);
  - audio by an automated test (heard by hand).
- **Saves** live in the browser's storage: clearing site data or a private window loses them. Export a copy.
- **Not tested at all:** Firefox and Safari, mobile browsers, and the 32-bit native builds, rendering on a real GPU and the PS2 matching build. The last three are native, outside this release.

## Verification of this release

The detailed results, with the commands used, are in these documents:
- [UPSTREAM_REVIEW_0.1.32_0.1.33.md](UPSTREAM_REVIEW_0.1.32_0.1.33.md): the merges;
- [BROWSER_MIGRATION_PLAN.md](BROWSER_MIGRATION_PLAN.md): M0–M9;
- [BROWSER_FEATURE_MATRIX.md](BROWSER_FEATURE_MATRIX.md): features and tests.

## Licence

The project's code is MIT (`LICENSE`); third-party licences are in [BROWSER_ONLY_AUDIT.md](BROWSER_ONLY_AUDIT.md) §3.1. The game itself, and its data, belong to their owners and are not part of this release.
