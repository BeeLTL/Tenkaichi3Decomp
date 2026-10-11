#!/usr/bin/env python3
"""The WebAssembly build (BT3_CC=wasm): the game's sources and the port's code compiled with Emscripten.

The game's sources take the native build's route as far as it applies (portsrc.prepare, eeconst.transform), then:
  - ptr32.r2l: call arguments evaluated right to left, as the game's compiler does (clang evaluates left to right;
    the native 64-bit build needs the same pass);
  - no ptr32 pointer narrowing: wasm32 pointers are 4 bytes, as on the PS2;
  - clang -O0 IR -> wasm_sf.py (the PS2's float arithmetic as calls, see there) -> optimised wasm object.
wasm32 already aligns long long / double to 8 bytes (what -malign-double gives the 32-bit x86 build).

Usage (emcc on PATH):
  python port/tools/wasm_build.py one <source.c>...   compile those game sources only (objects in port/build/obj_wasm)
  python port/tools/wasm_build.py game                compile every game source (portsrc.sources())
"""
import concurrent.futures, os, pathlib, subprocess, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import eeconst, portsrc, wasm_sf

ROOT = pathlib.Path(__file__).resolve().parents[2]
OBJ = ROOT / "port/build/obj_wasm"
EMCC = "emcc"
# The game's flags of undefined.py without the x86 ones (-m32, -msoft-float, -mno-sse, -malign-double, -fno-pic, ...).
GAME = ["-std=gnu89", "-fno-strict-aliasing", "-ffp-contract=off", "-fcommon", "-w", "-fno-builtin",
        "-Wno-error=return-mismatch", "-Wno-error=incompatible-function-pointer-types", "-Wno-error=int-conversion",
        "-Wno-error=implicit-function-declaration", "-Wno-error=incompatible-pointer-types"]
FORCED = ["-include", "port_libm.h", "-Iinclude", "-Iport/include", "-include", "port_compat.h"]
OPT = ["-O2"]

def run(cmd):
    return subprocess.run([str(c) for c in cmd], cwd=ROOT, capture_output=True, text=True, encoding="latin-1")

def compile_soft(src, o, extra=(), r2l=True):
    """One software-float source -> wasm object. Returns (ok, message, lowered operation count)."""
    extra = list(extra)
    r = run([EMCC, "-E"] + GAME + FORCED + extra + [src])
    if r.returncode:
        return False, "preprocess: " + r.stderr.strip()[-600:], 0
    # Windows: line markers carry paths with single backslashes, which a second parse reads as escape sequences
    text = eeconst.transform(LINE_MARK.sub(lambda m: m.group(1) + m.group(2).replace("\\", "/") + '"', r.stdout))
    i = o.with_suffix(".i")
    if r2l:
        import ptr32
        i.write_text(text, encoding="latin-1", newline="\n")
        try:
            text = ptr32.r2l(i.read_bytes(), ["-x", "cpp-output", "--target=wasm32-unknown-emscripten",
                                               GAME[0], "-w", "-Wno-error=return-mismatch",
                                               "-Wno-error=return-type"]).decode("latin-1")
        except RuntimeError as e:
            return False, f"r2l: {e}", 0
    i.write_text(text, encoding="latin-1", newline="\n")
    ll = o.with_suffix(".ll")
    r = run([EMCC, "-S", "-emit-llvm", "-O0", "-Xclang", "-disable-O0-optnone"] + GAME + extra +
            ["-x", "cpp-output", i, "-o", ll])
    if r.returncode:
        return False, "compile: " + "\n".join(l for l in r.stderr.splitlines() if "error" in l)[:800], 0
    text, n, bad = wasm_sf.fix(ll.read_text(encoding="latin-1"))
    if bad:
        return False, "float operations without a PS2 routine: " + "; ".join(l for _, l in bad[:3]), n
    sf = o.with_suffix(".sf.ll")
    sf.write_text(text, encoding="latin-1", newline="\n")
    r = run([EMCC, "-c"] + OPT + [sf, "-o", o])
    if r.returncode:
        return False, "codegen: " + r.stderr.strip()[-600:], n
    # the object itself: no f32 arithmetic, comparison or conversion may be left (neg / abs / copysign are sign-bit
    # operations, load / store / select / reinterpret keep the pattern)
    r = run(["llvm-objdump", "-d", o])
    left = sorted(set(F32_OP.findall(r.stdout)))
    if r.returncode or left:
        return False, f"f32 instructions in the object: {left or r.stderr[-300:]}", n
    return True, "", n

LINE_MARK = __import__("re").compile(r'^(# \d+ ")([^"\n]*)"', __import__("re").M)
F32_OP = __import__("re").compile(r"\bf32\.(add|sub|mul|div|eq|ne|lt|le|gt|ge|convert_\w+|trunc\w*|demote_f64|min|max|sqrt|nearest|floor|ceil)\b")

def game_object(f):
    f = pathlib.Path(f).resolve()
    o = OBJ / (str(f.relative_to(ROOT / "src")).replace("\\", "/").replace("/", "_")[:-2] + ".o")
    src, done, missing = portsrc.prepare(f)
    ok, msg, n = compile_soft(src, o, [f"-I{f.parent}"])
    return f, o if ok else None, msg, n, missing

def main():
    OBJ.mkdir(parents=True, exist_ok=True)
    if (len(sys.argv) > 2 and sys.argv[1] == "one") or sys.argv[1:] == ["game"]:
        # (portsrc.REPLACED holds "/" paths and relative_to() gives "\" ones on Windows: filtered here as well)
        fs = sys.argv[2:] if sys.argv[1] == "one" else \
            [f for f in portsrc.sources() if f.relative_to(ROOT).as_posix() not in portsrc.REPLACED]
        with concurrent.futures.ProcessPoolExecutor(min(16, os.cpu_count() or 4)) as ex:
            res = list(ex.map(game_object, fs))
        bad = 0
        for f, o, msg, n, missing in res:
            rel = f.relative_to(ROOT)
            print(f"{'ok    ' if o else 'FAILED'} {rel}  ({n} float operations lowered){'  no C for: ' + ' '.join(missing) if missing else ''}")
            if not o:
                bad += 1
                print("       " + msg.replace("\n", "\n       "))
        print(f"{len(res) - bad} of {len(res)} compiled, {sum(r[3] for r in res)} float operations lowered")
        sys.exit(1 if bad else 0)
    print(__doc__)
    sys.exit(2)

if __name__ == "__main__":
    main()
