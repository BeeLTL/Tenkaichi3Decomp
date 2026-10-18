#!/usr/bin/env python3
"""Writes port/build/web/bt3.mem for the browser build: where the GAME's own variables lie in the linked bt3.wasm, in
the format port/tools/make_state.py writes for the native programs (port/src/gs/state.c reads it: what a checksum of
the game state covers).

Reads wasm-ld's map (port/build/bt3_wasm.map, -Wl,-Map in wasm_build.py LINK): its input sections, each with its
object, in the output sections .data, .bss and .tdata. A section belongs to the game by the same rule as natively
(make_state.py is_game): the game's objects and the data tables, and of the port's own pc_* files only those whose
variables are part of what the game goes through (make_state.PORT_STATE); not the renderer, the page bridge, the
sound and the rest (gs_*, web_*, the other pc_*). Ranges are merged over alignment gaps, never over a port variable.

Usage: python port/tools/wasm_state.py   (run by wasm_build.py link)"""
import bisect, itertools, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "port/tools"))
from make_state import PORT_STATE

MAP = ROOT / "port/build/bt3_wasm.map"
OUT = ROOT / "port/build/web/bt3.mem"
WRITABLE = (".data", ".bss", ".tdata")


def is_game(obj):
    p = pathlib.PurePath(obj.replace("\\", "/"))
    if p.parent.name == "obj_wasm_data":
        return True
    if p.parent.name != "obj_wasm" or p.name.startswith(("gs_", "web_", "cxx_")):
        return False
    if p.name.startswith("pc_"):
        return p.stem[3:] in PORT_STATE
    return True


def main():
    text = MAP.read_text(errors="replace")
    out, ranges, others, marks = None, [], [], {}
    for line in text.splitlines():
        m = re.match(r"^\s*([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+) (.*)$", line)
        if not m:
            continue
        addr, size, rest = int(m.group(1), 16), int(m.group(3), 16), m.group(4)
        if not rest.startswith(" "):                  # an output section (".data", ".bss", ...)
            out = rest.strip()
            continue
        rest = rest.strip()
        if rest in ("gPortResim", "gPortVBlanks"):    # a symbol line
            marks[rest] = addr
            continue
        o = re.match(r"^(.*\.o):\((\S+)\)$", rest)   # an input section: "<object>:(<section>)"
        if not o or out not in WRITABLE or not size or not addr:
            continue
        (ranges if is_game(o.group(1)) else others).append((addr, addr + size))
    if set(marks) != {"gPortResim", "gPortVBlanks"}:
        sys.exit(f"wasm_state: {sorted({'gPortResim', 'gPortVBlanks'} - set(marks))} not in the map")
    ranges.sort()
    others.sort()
    starts = [x for x, _ in others]
    reach = list(itertools.accumulate((y for _, y in others), max))
    merged = []
    for a, b in ranges:  # (as make_state.py: across alignment gaps, never across a port variable)
        if merged and a <= merged[-1][1] + 64 and not ((n := bisect.bisect_left(starts, a)) and reach[n - 1] > merged[-1][1]):
            merged[-1][1] = max(merged[-1][1], b)
        else:
            merged.append([a, b])
    OUT.write_text(f"0 {marks['gPortResim']:x}\n1 {marks['gPortVBlanks']:x}\n" + "".join(f"{a:x} {b - a:x}\n" for a, b in merged))
    print(f"{OUT.name}: {len(merged)} ranges of game variables, {sum(b - a for a, b in merged):,} bytes")


if __name__ == "__main__":
    main()
