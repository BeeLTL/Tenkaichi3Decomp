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

## 3. Log

- 2026-10-11: repository identified, audit above. Emscripten SDK 6.0.12 installed (local, `tools/emsdk`).
  **Blocker**: this session's working folder lives under a very long Windows path; `git clone` into subfolders
  fails (`'$GIT_DIR' too big`) and loading `libclang.dll` fails (WinError 206, path too long). The build writes
  deep trees under `port/build/`. Needs a short project path (e.g. `C:\project\...`) before building.

## 4. Not done yet (everything below is unverified)

- First wasm compile of a game source; the IR soft-float rewrite; data tables as wasm objects; link.
- Browser page, file loading, WebGL2 back end, audio, input, saves, movies, WebGPU back end, performance.
