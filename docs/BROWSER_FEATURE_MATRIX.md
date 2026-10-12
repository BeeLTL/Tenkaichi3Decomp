# Browser port: features and baseline tests

Baseline of 2026-10-12 at `browser-port` `31588d7`. The commands are in [BROWSER_BUILD.md](BROWSER_BUILD.md) §4, the findings in [BROWSER_ONLY_AUDIT.md](BROWSER_ONLY_AUDIT.md), and the earlier history in [BROWSER_PORT_STATUS.md](../BROWSER_PORT_STATUS.md).

Environment:
- Windows 11 Pro 10.0.26300, NVIDIA RTX 5070 Ti, Chrome 154.0.8037.98 (headless, via CDP), Node 24.19.0;
- Emscripten 6.0.12, Python 3.12.10, naga 30.0.1;
- served by `devserver.py` on 127.0.0.1:8080 with the user's own `gamedata/`.

## 1. Features

| Feature | Browser | Native (for reference) | Evidence |
|---|---|---|---|
| Boot, intro movie, title, menus | Yes | Yes | boot runs, both renderers |
| Battles (demo, versus) | Yes | Yes | demo runs; trace `8ad198bf58dbed33` |
| WebGL 2 renderer (default) | Yes | — (native: GL, Vulkan) | demo/boot shots |
| WebGPU renderer (`?gpu=webgpu`) | Yes | — | demo/boot shots identical to WebGL 2 |
| Widescreen, HUD layout, effect switches | Yes (page settings panel) | Yes (ImGui window) | status doc, settings section |
| Settings kept | Yes (localStorage) | settings file | status doc |
| Controllers: set-up, presets, live view, test | Yes | ImGui | `controllers.js` |
| Keyboard | Yes | Yes | `controllers.js` |
| Game saves kept, with backup, export and import | Yes (IndexedDB) | host files | `saves.js` |
| Performance dashboard, diagnostics export | Yes | — | `dashboard.js` |
| Smooth motion | Limited per renderer (measured) | Yes | status doc |
| Audio (ADX music, sound effects) | Yes | Yes | heard by hand; **no automated test** |
| Replays (`BT3_REPLAY`) and demo (`BT3_DEMO`) | Yes (`?env=`) | Yes | bench runs use `BT3_DEMO` |
| Unlock everything | Yes | Yes | `saves.js` step 3 |
| Game-data verification | **No: silently off** (audit §5.2-1) | Yes | code reading |
| Added stages and songs | **No: silently absent** (audit §5.2-2) | Yes | code reading |
| Texture packs | **No: silently absent** | Yes | code reading |
| `mods/` file overrides | **Probably yes, untested** | Yes | code reading |
| Online play | No: menu item hidden, `net_off.c` | Yes | by design |
| Vulkan, installer, release packaging | No | Yes | by design |

## 2. Baseline test results

| Test (command in BROWSER_BUILD.md §4) | Result |
|---|---|
| `wasm_sf/run.py` | **PASS** |
| `pipekey/run.py` | **PASS** |
| `wgsl/check.js` | **PASS**: 11 shaders |
| `web_data/test_data.js` | **PASS**. Then a libuv assertion when Node exits, after the result is printed. **Unknown — investigate further** |
| `wasm_mem/build.py` | Layout B **PASS** 17/17 (1,472 MB). The script's overall verdict reads FAIL because it scores the rejected layout A (224 MB). Not a regression (plan M6) |
| Build, variant `clean` | **PASS**, 129 s; byte-identical to the working build |
| Build, fresh clone | **PASS**, 133 s. `bt3.wasm` differs only by CRLF in the embedded shaders (plan M5). Runs identically: demo 12/12 on both renderers, same trace |
| Demo, WebGL 2 (`bench.js --label audit_base_gl --headless --shots 150`) | **PASS**: trace `8ad198bf58dbed33`, 12/12 identical to `p5_gl`. The mean of 19.9 ms was measured during a parallel build, so it is not a valid timing |
| Demo, WebGPU (`... --label audit_base_wgpu --query gpu=webgpu`) | **PASS**: same trace, 12/12 identical to `p5_wgpu`; mean 6.74 ms |
| Boot, both renderers (`--boot --from 600 --to 3000 --shots 200`) | **PASS**: 12/12 identical to `boot_after` |
| `web/dashboard.js` (WebGL 2) | **PASS** (24 checks). Work 13.0 / 15.5 / 26.0 ms (mean / p95 / p99); GPU 10.1 ms; 30.0 ticks/s |
| `web/dashboard.js --query gpu=webgpu` | **PASS** (24 checks). Work 6.4 / 8.6 / 10.2 ms; GPU 1.0 ms; 30.0 ticks/s |
| `web/controllers.js` | **PASS** (simulated controllers; no physical hardware) |
| `web/saves.js` | **PASS** (18 checks): the game's own New Game save, reload, overwrite with backup, export/import (valid, malformed, checksum failure, cut short), recovery, aborted write, damaged store, no IndexedDB, quota full |
| `bench/present_rate.js` (both renderers) | **Measured** (a report, no pass/fail); headless, ~196 Hz refresh. WebGL 2: 29.6–29.8 shown/s at every smooth-motion setting (0–3), work 12.5–13.4 ms mean. WebGPU: setting 0 29.6 shown/s, work 5.5 ms; settings 1–3 58.4–59.6 shown/s, work 12.5–14.3 ms. Game ticks 30/s and speed 100 % throughout |
| `ps2float` | **BLOCKED**: native; needs the PS2 toolchain and PCSX2 data |
| Native build and run | **BLOCKED**: no gcc, clang or mingw on this machine (plan M0) |
