# AGENTS.md

Native PC port of DBZ Budokai Tenkaichi 3 (PS2, SLUS-21678): the decompiled game code compiled for PC, with a new
renderer, sound and input layer underneath. Not an emulator. Successor of `../BT3-Recomp` (static recompilation);
nothing from there applies here unless re-checked.

## Two kinds of work

Decide which one the task is before touching anything; they have different rules.

- **Decomp work** (matching C for `src/`, `include/`, `config/symbols/`): follow `docs/agent_rules.md` and
  `docs/decomp_guide.md` exactly. Done = `python3 scripts/fdiff.py <file.c>` prints `OK` for every function, or the
  function is `INCLUDE_ASM` with the attempt in `#if 0`. Background per area: `docs/systems/<area>.md`.
- **Port work** (everything under `port/`, plus PC changes inside `#ifdef PORT`): read `docs/port/README.md`
  first; it is the running log of what is done and verified. Online play: `docs/netplay_notes.md`. Known
  problems: `docs/known_bugs.md`.

## Port invariants

These hold across the whole port; a change that breaks one breaks the game silently.

- **Fixed low addresses.** The game's pointers are 4 bytes. PS2 memory lives at its original addresses
  (`port/src/plat_mem.c`): heap 0x003BE000–0x02000000, game stack 0x02000000, registers 0x10000000/0x12000000,
  low arena 0x13000000, scratchpad 0x70000000. The program is linked at 0x20000000 without relocation
  (`port/tools/link.py`). Every allocation the game sees must stay below 4 GB.
- **PS2 arithmetic is exact.** Floats go through `port/src/softfloat_ps2.c` to reproduce the EE's non-IEEE
  behaviour; replays and online play depend on bit-identical results. Verify a change touching game logic by
  running a replay (`port/run.sh`) and comparing state (`port/tools/compare_state.py`, `compare_hash.py`).
- **Game code changes are fenced.** Edits to `src/`/`include/` go inside `#ifdef PORT` so the decomp still
  matches.
- **A new platform changes nothing for the existing ones.** Its code sits behind its own guard (`__APPLE__`,
  its `BT3_CC` variant); the Linux and Windows builds keep their toolchain and give the same demo-fight state
  hashes before and after the change. The new platform reproduces the same hashes, since replays and online
  matches between platforms depend on it.
- **No game data in the repo.** `port/data/` holds table shapes only; values come from the user's disc at
  start-up. `gamedata/`, `disc/`, `asm/`, `build/` are generated and ignored.

## Build variants

`BT3_CC` selects the build (`port/tools/toolchain.py` is the source of truth): `gcc` (32-bit Linux), `clang64`
(64-bit Linux, the release one), `win64` (mingw). `python3 install.py --check` lists missing tools;
`python3 install.py --iso <image>` does the full Linux build plus a demo-fight test. Releases:
`port/release/build.sh` (docker only).

## Platforms

Supported: Linux x86-64 (glibc ≥ 2.35) and Windows 10/11 x86-64. macOS is in progress: read
`docs/port/macos_plan.md` before any macOS task (native arm64, `BT3_CC=mac64`: game addresses are offsets in a window at
1 TB, applied by an LLVM pass). Guard macOS code with `__APPLE__`, next to the existing `_WIN32` branches, leaving the Linux path
as it is.

## Conventions

- Commit messages: `Area: what changes for the player (why, with a number if measured)`, as in `git log`.
- Comments in the port explain *why* in plain English sentences, matching the surrounding files.
- Keep this file current: when a task teaches a non-obvious rule (a gotcha, an invariant, a dead end), add one line
  here or in the doc it points to.
