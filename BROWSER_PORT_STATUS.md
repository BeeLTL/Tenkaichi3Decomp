# Browser port: status

Working log for the WebAssembly / browser build of Tenkaichi3Decomp. "Verified" means checked by running or by
reading the source in this checkout; everything else is marked as a plan or an assumption.

## 0. Repository (verified 2026-10-11)

- `https://github.com/HeckinChloe/tenkaichi3-decomp` **does not exist** (`git ls-remote`: "Repository not found";
  GitHub API: user `HeckinChloe` not found).
- The project matching the description (`port/src/gs/`, `docs/port/README.md`, `port/tools/`, Vulkan renderer,
  Dear ImGui) is **`https://github.com/z3xox/Tenkaichi3Decomp`** (MIT for the port's code, default branch `main`,
  last push 2026-10-11). It is a PC port built on the matching decompilation `z3xox/BT3-Decompiled`.
  Unrelated sibling: `z3xox/BT3-Recomp` (static recompilation, GPL-3.0); not used.
- Checked out shallow (`--depth 1`) at the head of `main` on 2026-10-11.

## 1. Audit (verified against the source)

### Build system
- No CMake/Make. Python drivers: `port/tools/undefined.py` (compile everything) then `port/tools/link.py` (link,
  generate stubs, write `.dat`). Variants in `port/tools/toolchain.py` (`BT3_CC=gcc|clang|clang64|win64`).
- Game code (`src/`, 273 C files incl. the menu overlay) is preprocessed, rewritten by `eeconst.py` (PS2 compiler's
  truncated float constants), then compiled. Native 64-bit builds also go through `ptr32.py` (4-byte pointers via
  `__ptr32`) and `irfix.py` (LLVM IR repairs) and `llc`.
- The game's data tables are not in the repo: `port/data/*.s` hold their *shape* (labels, `.space`, `.zero`,
  `.long sym`, 72 files; only those 5 directives + `.globl/.section/.p2align`). Values are copied in at start from
  the user's disc files (`Port_LoadGameData`, `<program>.dat` made by `make_dat.py`).

### Float model (the main compile-time issue for wasm)
- All game code is built `-msoft-float`: every single-precision op becomes a call (`__addsf3`, `__mulsf3`,
  `__divsf3`, `__ltsf2`, `__fixsfsi`, `__floatsisf`, ...) implemented in `port/src/softfloat_ps2.c` with PCSX2's
  PS2 model (truncation, no NaN/Inf/denormals). Routines take/return `uint32_t` bit patterns.
- Doubles use host IEEE arithmetic even natively (`port/src/plat_libm.c`).
- **clang's wasm32 target has no soft-float mode.** Plan: emit LLVM IR, rewrite every `float` `fadd/fsub/fmul/fdiv`,
  `fcmp`, `fptosi/fptoui/sitofp/uitofp` into `bitcast -> call @__xxsf3(i32, i32) -> bitcast` (bit-exact: wasm's
  reinterpret ops preserve every pattern), leave `double` ops native, then optimise and compile the IR. This is the
  same lowering the backend does natively, done before the optimiser instead of after (so no IEEE constant folding
  of float ops can sneak in).

### Memory model
- The game stores pointers in 32-bit fields: **wasm32 is a natural fit** (no `ptr32.py` needed). wasm32 also aligns
  `long long`/`double` to 8, which is what `-malign-double` provides on i386 (`check_layout.py` exists to verify).
- Fixed addresses used (`port/src/plat_mem.c`): game heap `0x003BE000..0x02000000` (one bump allocator,
  `Port_Malloc`), hardware registers `0x10000000` (64 KB) and `0x12000000` (8 KB), scratchpad `0x70000000` (16 KB).
  Plan: place the wasm program's own data above all of these (`-sGLOBAL_BASE`), so the PS2 regions are plain linear
  memory at their real addresses. Cost: the wasm memory must be ~1.8 GB+ (pages are committed lazily by browsers).
- `Port_LowAlloc` exists for 64-bit only; on wasm32 plain `malloc` qualifies (all addresses < 4 GB).

### Main loop and threads
- The game's own `main` runs the whole program; it blocks once per vertical blank in `vblank_wait()`
  (`port/src/plat_stub.c`, `nanosleep`). Plan: on wasm that wait yields to the browser (`emscripten_sleep` under
  JSPI, or Asyncify), so the original loop is kept unchanged.
- pthreads: optional render thread (`BT3_GS_THREAD`, off by default), data verification thread (`plat_verify.c`),
  netplay threads, `pthread_once`/mutex in settings and input. Plan: single-threaded wasm build; the verification
  thread and netplay are compiled out; `pthread_once`/mutexes work in Emscripten without threads.
- 64-bit stack switch (`__wrap_main`) is x86-64/Windows only; not needed on wasm32.

### Renderer
- Shared recorder `port/src/gs/gs_draw.c` (GS emulation front end, VU1 programs as shaders/interpreter, texture
  cache) + two back ends: Vulkan via **SDL3 GPU** (`gs_gpu.c`, SPIR-V) and **OpenGL 3.3 core** (`gs_gl.c`, GLSL
  translated at run time from the same Vulkan GLSL sources). 9 shaders in `port/src/gs/shaders/`.
- GL entry points used (complete list from `gs_gl.c`): all exist in WebGL2/GLES 3.0 except `glClipControl`
  (optional, has a fallback; WebGL `EXT_clip_control`), `GL_DEPTH_CLAMP` (WebGL `EXT_depth_clamp`), indexed
  `glColorMaski/glEnablei/glDisablei` (WebGL `OES_draw_buffers_indexed`), S3TC textures (texture packs only).
- Settings UI: Dear ImGui (vendored) with SDL GPU / OpenGL3 backends.

### Files, audio, input
- All game data reads go through two functions, `Port_FileSize` / `Port_FileReadAt` (`plat_fcache.c`): one choke
  point for browser-side loading. Data root `gamedata/` (~4 GB unpacked, 68,611 files).
- Audio: SDL3 audio streams (ADX music/voices `snd_adx.c`, sound effects sampler `snd_se.c`).
- Input: SDL3 keyboard and gamepads (`gs_input.c`). Saves: `saves/card1/` folder (`plat_mc.c`). Settings:
  `bt3_settings.txt` (`plat_settings.c`). Movies: own MPEG-2 intra decoder (`plat_mpeg2.c`, no ffmpeg).

### Known limitations of the project itself (from docs/port/README.md, not re-tested here)
- `ObjSeam_TransformVtx` was the last game function without C; a later note says it was written (2026-10-06).
- The two-layer character draw order, render thread race, OpenGL is ~14% slower than Vulkan (driver call count).
- Online play exists (out of scope for the browser build).

## 2. Plan and decisions

1. **Renderer: WebGL2 first, through the existing GL 3.3 back end** (smallest real path: the same recorder, the
   same shaders after a GLSL ES translation). **WebGPU second**, as a third back end of `gs_draw.c` next to Vulkan
   and GL (the recorder already abstracts the back end), with the 9 shaders ported to WGSL. Rust/wgpu is not
   planned: the renderer is C, the back-end interface is a C function table, and a C WebGPU back end (Emscripten's
   `webgpu.h`) needs no FFI layer.
2. Toolchain: Emscripten (installed: 6.0.12, clang 24), a new variant `BT3_CC=wasm` in `toolchain.py`; the native
   variants untouched.
3. Data: never bundled into public artifacts. Private build serves the user's own unpacked `gamedata/` from a local
   server; the browser fetches files on demand (`Port_FileReadAt` -> fetch with Range) and caches them.

## 3. Environment (verified 2026-10-11)

| | |
|---|---|
| Machine | Windows 11 Pro 10.0.26300, AMD Ryzen 7 7700 (8 cores), 31 GB RAM, NVIDIA RTX 5070 Ti |
| Layout | `C:\project\Tenkaichi3Web\` = `Tenkaichi3Decomp\` (git, branch `browser-port`), `tools\emsdk\`, `tools\venv\`, `env.sh` |
| Emscripten | 6.0.12 (clang 24.0.0git, `llvm-objdump`, `llvm-nm`, `wasm-ld`), node 24.19.0 (bundled) |
| Python | 3.12.10, venv with `libclang` (pip; loads from the short path) |
| Git | 2.45.2.windows.1 (`core.autocrlf` is on in this user's config) |
| Browsers | Chrome 154.0.8037.98, Edge 155.0.4283.45, the app's built-in Chromium 152; no Firefox installed |

The long-path problem (git `'$GIT_DIR' too big`, `libclang.dll` WinError 206) came from the session folder's path;
it does not occur under `C:\project\Tenkaichi3Web`. emsdk was moved there (not reinstalled) and works.

```
source /c/project/Tenkaichi3Web/env.sh             # Git Bash: emcc, node, venv python on PATH
python port/tests/wasm_sf/run.py --control          # software-float lowering test (+ IEEE control)
python port/tools/wasm_build.py game                # all game sources -> port/build/obj_wasm
python port/tests/wasm_mem/build.py                 # memory test under node; then in browsers:
python port/web/devserver.py --port 8080            # serves port/build/web on 127.0.0.1 only
#   http://127.0.0.1:8080/memtest/memtest.html?layout=B&tag=<name>  -> port/build/web/__results/
```

## 4. Verified results

### Software float (Priority 2): PASS
- `port/tools/wasm_sf.py` lowers -O0 IR before optimisation: `fadd/fsub/fmul/fdiv` -> `__addsf3/...`; `fcmp` (all
  16 predicates) -> `__ltsf2` + `icmp` (`ord` true, `uno` false: the PS2 has no unordered values, as the native
  routines say); `fptosi/fptoui` (<= 32 bits) -> `__fixsfsi/__fixunssfsi`; `sitofp/uitofp` (<= 32 bits) ->
  `__floatsisf/__floatunsisf`. Kept native, as in the native build: double arithmetic, float <-> double, `fneg`,
  `fabs`, `copysign` (sign-bit operations). Anything else on floats (frem, 64-bit conversions, float vectors, other
  float intrinsics), and any float instruction still present after the rewrite, stops the build.
- `port/tests/wasm_sf/run.py --control`: plain C operators through the lowering vs the routines called directly.
  Inputs: 43 special patterns (zeros, denormals, max, Inf/NaN patterns, 2^31 limits, ...) in every pair, 1,000,000
  random pairs, 2,000,000 single values for conversions, constant operands. **0 differences** in every operation
  (add, sub, mul, div, madd, neg, fabs, 14 comparison forms, select, ftoi/ftou/ftos16/ftou8, itof/utof/s16tof/u8tof,
  bit-pattern pass-through incl. a signalling NaN pattern, constants). Against PCSX2's own results
  (`port/tests/ps2float/pcsx2_v2.7.303.txt`, 1500 cases x 7 FPU columns): **0 of 10,500 differ**. 20 hand-worked
  PS2-vs-IEEE cases pass (1/0 = 0x7F7FFFFF, max*2 = max, denormal+0 = 0, NaN pattern > 1 is true,
  (int)NaN pattern = INT_MAX, the FPU add's exponent-gap rules, ...). The optimised object has **no** f32
  arithmetic / compare / convert instruction. Control: the same C compiled with host IEEE floats fails (2,091 of
  10,500 PCSX2 values, 12 hand cases), so the test does see the difference.
- Found and fixed on the way: clang 24 prints float constants as `f0x3F800000`. The first version of the rule did
  not match them and left 57 operations of `btl_char_move.c` as host floats. The safety net (no float instruction
  may remain) and the constant-operand tests were added because of it.
- One of my own hand-worked expectations was wrong (FPU add with an exponent gap >= 25 drops the smaller operand:
  1 - 2^-30 = 1.0); the routines were right, the expectation was corrected.
- Not covered: sqrt (a libm call, `Port_sqrtf`, not an operator); `frem` and 64-bit float conversions (none appear
  in the game's code; the build would stop if one did).

### All game sources compile (Priority 4, part 1): 269 of 269
- `python port/tools/wasm_build.py game`: 269 of 269 game sources (273 minus the 4 replaced vector files), 14,841
  float operations lowered, each object checked with `llvm-objdump` for f32 arithmetic. About 40 s.
- Fixes needed: `emcc -E` line markers on Windows hold single backslashes, which the second parse (libclang, r2l)
  read as escapes (`\o`, `\u`): normalised to `/`. libclang needs `-Wno-error=return-mismatch` (three functions
  without a return value). `portsrc.REPLACED` holds `/` paths, so on Windows the four replaced files were not
  excluded: filtered in wasm_build.py (portsrc.py left unchanged).
- Flags that hide diagnostics, as the native build's `-w` does: `-Wno-error=` return-mismatch,
  incompatible-function-pointer-types, int-conversion, implicit-function-declaration, incompatible-pointer-types.
  These matter more on wasm than on x86: a call through a pointer of the wrong function type TRAPS on wasm
  (`call_indirect` checks the signature). To be found at link time (wasm-ld) and at run time.
- Not yet linked or run: compiling is not a working build.

### Memory model (Priority 3): layout B chosen, PASS in node and three Chromium browsers
- Fixed addresses the game uses (plat_mem.c, include/sys/dma.h): heap 0x003BE000..0x02000000, hardware registers
  0x10000000 (64 KB; e.g. `VIF1_STAT = 0x10003C00` in game code), GS registers 0x12000000 (8 KB), scratchpad
  0x70000000 (16 KB). The scratchpad has no user in the game's C (no address literal; `Dma_SendVif1Spr` has no
  caller); the port maps it and `gs/state.c` snapshots it.
- The DMA code marks scratchpad lists with address bit 31 (`chain | 0x80000000`), so no address the game sees may
  be at or above 2 GB.
- **Layout B** (chosen): `-sGLOBAL_BASE=0x12010000 -sINITIAL_MEMORY=320MB -sALLOW_MEMORY_GROWTH
  -sMAXIMUM_MEMORY=0x70000000`. PS2 regions at their real addresses; the program's data, 16 MB stack and C heap
  above the GS registers; memory ends exactly at the scratchpad address, so any access to it traps ("memory access
  out of bounds") instead of landing in the C heap. Up to 1472 MB of C heap.
- **Layout A** (rejected on measurement): program above 0x70004000, everything at PS2 addresses, but under the 2 GB
  cap only 224 MB of C heap is left.
- `port/tests/wasm_mem`: 17 checks (sizes, 8-byte alignment of long long / double, struct and bit-field layouts,
  data / stack / malloc placement, each fixed region zero at start and holding a pattern, no collateral writes,
  pointer <-> u32 round trip, the PS2 heap address as a pointer, the cap), a scratchpad probe, growth until malloc
  returns NULL:

  | | node 24.19 | Chrome 154 headless | Edge 155 headless | built-in Chromium 152 |
  |---|---|---|---|---|
  | layout B | PASS, 1472 MB | PASS, 1472 MB in 30 ms | PASS, 1472 MB in 14 ms | PASS, 1472 MB in 17 ms |
  | layout A | 17/17, 224 MB only | 17/17, 224 MB: FAIL | 17/17, 224 MB: FAIL | not run |

  Instantiation with 320 MB initial memory: 4 to 7 ms. Every browser also allocated a bare 4 GB and 2 GB
  `WebAssembly.Memory` on this 31 GB machine, so allocation failure could not be provoked here; the loader will
  catch instantiation errors and show them. Not tested: Firefox, Safari, machines with little RAM.
- **Struct layouts**: the repository's `check_layout.py` against wasm32 (emcc -fsyntax-only through a wrapper; the
  tool unchanged): 14,862 documented PS2 member offsets in 183 headers, **1 differs** (`EftTask.unk4`, the same
  single report the native notes give for 32-bit x86 with `-malign-double`), 8 errors from `eft_z.h` (the tool
  asserts on a type declared later in that header; not a layout result).

### Platform code, data tables, link (Priority 4, part 2): the whole program links — 2026-10-11
Commands (Git Bash, `source /c/project/Tenkaichi3Web/env.sh`, in `Tenkaichi3Decomp/`):
```
python port/tools/wasm_build.py game     # 269 game sources
python port/tools/wasm_build.py port     # 62 port files (port/src, gs/, web/, newlib maths, ImGui)
python port/tools/wasm_build.py data     # 72 data tables + the list of their disc runs
python port/tools/wasm_build.py link     # stubs, wasm_sig.py, link -> port/build/web/{bt3.js,bt3.wasm,index.html,bt3_data.js}
```
Results: game 269/269, port 62/62 (584 float operations lowered there), data 73/73, **link OK**: `bt3.wasm`
4.7 MB, `bt3.js` 428 KB; 0 signature mismatches left; 103 stubs (`port/build/wasm_stubs.txt`).

- **Not built for the browser** (kept for the native builds): `gs/net.c`, `net_match.c`, `net_relay.c` (online play:
  UDP sockets, threads, matchmaking), `gs/gs_gpu.c` + `imgui_impl_sdlgpu3.cpp` (Vulkan through SDL GPU: SDL3 has no
  browser back end for it). `port/src/web/net_off.c` is the online-play interface in the state the native builds are in
  with no match (inactive, no names; `Port_NetView` / `Port_HudSwap` keep their non-network logic); `Port_NetInput`
  aborts with a message if ever reached (natively only called during a match).
- **SDL3**: Emscripten's port, SDL 3.4.2 (native builds use 3.4.12). Renderer: the GL back end on WebGL 2
  (`gs_draw.c` browser branch); ImGui's OpenGL3 back end with `#version 300 es` (`ui.cpp` browser branches).
- **Every file goes through LLVM IR** (soft-float files additionally through `wasm_sf.py`).
- **Data tables** (`port/tools/wasm_data.py`): one wasm data section per table file (contiguous, in order, 16-byte
  aligned, every label with its size), `.space`/`.zero` -> `.skip`, `.long sym+k` -> `.int32`, `D_3BE71C` -> the
  constant 0x3BE71C (natively `--defsym`). Function addresses in tables carry a placeholder `.functype`; checked with
  two objects in both link orders that wasm-ld still puts the real function's table slot there. The list of disc runs
  is generated as C (`port/build/gen_wasm/data_list.c`, offsets only) and linked; `plat_mem.c` (browser branch) turns
  it into the bytes of `<program>.dat` and runs the native loader code on it. 2,388 runs, 210,773 bytes, 1,872
  addresses: the figures of the native notes.
- **Signature mismatches** (`port/tools/wasm_obj.py`, `wasm_sig.py`): the game declares functions locally, not
  always as defined (`void f()` for `int f(int)`, one argument too many, ...). Harmless on the PS2 and x86; on wasm,
  wasm-ld replaces each such direct call by a trap. 171 functions, **813 call sites in 116 objects** repaired in the
  caller's IR to the definition's type (result dropped or read as 0, missing arguments 0, extra ones dropped, int/float
  bit-for-bit as the native software-float build passes them). Every repair is listed in `port/build/wasm_sig.txt`.
  Calls through function pointers are not covered: a mismatch there traps at run time (the page shows where).
- Other fixes: `main.c` compiled with `MainMenu_StartBallLoops` (a function used as the overlay's load address;
  the port's `sceRead` never writes there) mapped to a byte array; `heap.c` with `D_3BE71C` mapped to a pointer to an
  array at 0x3BE71C; `headless.c` with `__data_start`/`_end` mapped to wasm-ld's `__global_base`/`__data_end`;
  `plat_crash.c` browser branch (`Port_CrashContext` for the page); `newlib_shim.h`: `Port_scalbnf` prototype for the
  browser build.
- **Probable native bug, recorded, not changed natively**: `newlib_shim.h` includes `<math.h>` before renaming
  `scalbnf`, so `ef_pow.c` and `kf_rem_pio2.c` call `Port_scalbnf` without a prototype (implicit `int f()`: the float
  argument is passed as a double and the result read as an int). Affects `powf`'s subnormal path and `sinf`/`cosf` of
  large arguments in the native builds. The browser build declares the prototype.
- **Stubs**: 103 symbols nobody defines (98 SDK / CRI library functions in the game's address range, CRI's
  `dvCi*`/`htCi*`/`mfCi*` file-system drivers, `sceCdSearchFile`), most referenced only by library data tables. As
  natively (`link.py`), each stub prints `bt3: not implemented yet: <name>` and aborts: reaching one stops the game
  visibly, it cannot make start-up look successful.

### Game data in the browser (Priority 6/7, first part) — implemented, tested without game data
- All game-data reads in the port go through `fopen` (plat_fcache.c, snd_adx.c, snd_se.c, plat_mpeg2.c, plat_mem.c).
  The program is linked with `--wrap=fopen` (`port/src/web/web_fs.c`): a file in Emscripten's file system (saves,
  settings, prefetched files) opens as always; a read-only file under the data root that is not there opens as a
  stream (`fopencookie`) whose reads call the page's loader and WAIT for it (JSPI) without blocking the page.
- Why not the alternatives: synchronous XHR blocks the main thread; Emscripten's lazy files work only in workers;
  WasmFS's fetch backend needs pthreads (shared memory) for everything. JSPI cannot suspend before `main` (the
  constructor `Port_MapMemory` reads `disc/SLUS_216.78` and `disc/BIN/DBZP.BIN`): the page fetches those two first and
  writes them to the in-memory file system. Every other data read happens on the game's thread (the sound players
  load a whole file when they start; the audio callbacks only decode from memory).
- `port/web/bt3_data.js`: Range requests in 256 KB blocks, LRU cache (256 MB), sizes from `manifest.json` or `HEAD`.
  `node port/tests/web_data/test_data.js` (own HTTP server with scripted faults): **16 of 16 pass** — sizes by HEAD,
  URL encoding, 404 -> missing, 500 -> error, reads inside / across blocks / at and past the end, cache hit, LRU
  eviction and refetch, cache limit, concurrent reads sharing one request, server without Range (200, sliced),
  short body (error, not wrong data), 500 during a read.
- `port/web/devserver.py` serves the build and mounts the private data folder (`--mount gamedata=<folder>`; in Git
  Bash write it without a leading `/`, which MSYS would rewrite into a Windows path). It binds to 127.0.0.1 only.
- `plat_stub.c` browser branch: the vertical-blank wait is `emscripten_sleep` (a zero sleep when late), so the page
  gets control every blank: the frame is shown, input and sound are handled.

### Runtime test without game data (2026-10-11, built-in Chromium 152)
With a fake data folder (a 4 KB ELF header and 2 MB of zeros, nothing from the game) served at /gamedata:
checks pass (WebAssembly, JSPI, WebGL 2.0, data reachable), the two files are fetched, the module instantiates with
384 MB, the constructors run: memory map accepted, the two files read through the C library, all 2,388 runs copied,
and the engine's own check stops with its native message ("the game's programs in your game data are not the
unmodified USA release (SLUS-21678)"). With no data folder at all, the page says so before starting.
**Not yet run: `main`.** That needs the real data.

## 5. Log

- 2026-10-11 (1): repository identified, audit above, Emscripten installed. Blocked by the session folder's long path.
- 2026-10-11 (2): moved to `C:\project\Tenkaichi3Web`; environment verified; software-float lowering and its test;
  all game sources compile; memory model tested in four runtimes; struct layouts checked. Commits 8cd6f9e, 2593069,
  5527e58.

- 2026-10-11 (3): platform code, data tables, signature repair, link, data on demand, page; tested without game
  data. Commits e9cc31c, 2e5805c, 79be83e, 14b9b22.

## 6. Blockers and next milestone

**Blocker for the initialisation milestone: the game data.** The next test runs `main`, which needs the real
`gamedata/` (the native integrity check refuses anything else, as it should). Plan once the ISO is supplied:
`7z` (installed via scoop) + `port/tools/extract_disc.py <iso> C:\project\Tenkaichi3Web\gamedata` (outside the
repository, never committed), then `devserver.py --mount gamedata=C:/project/Tenkaichi3Web/gamedata` and the page.

Next milestone: **the engine initialises in the browser** — `Game_Main`'s initialisation list completes and the
first frames run (the native notes list it: heap, graphics, sound, files, movie, common data, save, jobs, vector
unit, DMA buffers, pad, memory card, fonts, fade), shown by the engine's own log and `gPortStage`. Expected next
problems, in the order they would appear: a trap in a call through a mismatched function pointer; a library stub
reached; something the engine reads before the page's data loader is attached; the scratchpad snapshot in
`gs/state.c` (0x70000000 is outside memory on purpose).

Not started: saves and settings persistence (IndexedDB), canvas resizing / fullscreen, input and audio checks in the
browser, movie playback check, manifest generation, persistent cache, WebGPU evaluation, performance.
