#!/bin/bash
# Online play, natively: do two revisions behave the same? (docs/BROWSER_ONLINE_PLAN.md, P1: the transport seam.)
#   port/tests/native/net_compare.sh <rev A> <rev B> <gamedata folder> [work folder]
# Needs docker and git; the game data is the user's own (mounted read-only, never copied, never in the image).
# For each revision, in the release container (port/release/Dockerfile, built if missing): net_in_container.sh plays
# upstream's scripted scenarios (two copies over UDP on one machine: lockstep; 20% packet loss; 30% loss with 4 ms of
# lag; rollback 6 with 50 ms latency and 15% loss) with made-up input, the attract demo's fight played by two
# connected copies (lockstep; rollback with latency and loss), and one copy offline (the control). Each copy writes its
# state checksum every blank (BT3_HASH). Then, per scenario (the last line of each blank: a blank run again after a
# rollback is logged again):
#   - in each revision, host against joiner: the WHOLE state must be the same at every blank both reached;
#   - A against B: the fight's own values must be the same at every blank; the demo scenarios must have at least
#     1000 blanks of fighting. The whole-state checksum holds addresses of functions and variables, which move with
#     any change in code size (state.c says so), so between two builds it is reported, not required: the offline
#     control shows that even without any network;
#   - no copy may have ended a match with "no longer agree (desync)".
# Exit status 0 only if all of that holds and every run reached MIN_BLANKS. REUSE=1 keeps a revision's results.
# NET_SCENARIOS: which scenarios (default all).
set -euo pipefail
A=$1; B=$2; DATA=$3; WORK=${4:-$(mktemp -d)}
SECONDS_EACH=${SECONDS_EACH:-120}; MIN_BLANKS=${MIN_BLANKS:-1500}
SCENARIOS="${NET_SCENARIOS:-lockstep_d2 loss20_d2 loss30_lag4_d3 roll6_lat50_l15 demo_d2 demo_roll6 offline_demo}"
HERE=$(cd "$(dirname "$0")" && pwd); REPO=$(cd "$HERE/../../.." && pwd)
IMAGE=bt3-port-release:jammy
docker info >/dev/null 2>&1 || { echo "Docker is not running (start Docker Desktop): this check needs it" >&2; exit 2; }
docker image inspect $IMAGE >/dev/null 2>&1 || docker build -t $IMAGE "$REPO/port/release"
mkdir -p "$WORK/out"
host() { if command -v cygpath >/dev/null; then cygpath -w "$1"; else echo "$1"; fi; }
LAST_SC=${SCENARIOS##* }
for rev in "$A" "$B"; do
    if [ "${REUSE:-0}" = 1 ] && [ -s "$WORK/out/${rev}_${LAST_SC}_host.hash" ]; then echo "$rev: results reused"; continue; fi
    src="$WORK/src_$rev"
    rm -rf "$src" && git -c core.autocrlf=false clone -q "$REPO" "$src" && git -C "$src" checkout -q "$rev"
    MSYS_NO_PATHCONV=1 docker run --rm -v "$(host "$src"):/src" -v "$(host "$DATA"):/data:ro" -v "$(host "$WORK/out"):/out" \
        -v "$(host "$HERE"):/native:ro" -e NET_SCENARIOS="$SCENARIOS" $IMAGE sh /native/net_in_container.sh "$rev" "$SECONDS_EACH"
done
PY=$(python3 -c 1 >/dev/null 2>&1 && echo python3 || echo python)  # (Windows: python3 may be the Store stub)
$PY - "$WORK/out" "$A" "$B" "$MIN_BLANKS" $SCENARIOS <<'EOF'
import sys, pathlib
out, A, B, need, scen = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3], int(sys.argv[4]), sys.argv[5:]
ZERO = "0000000000000000"
def blanks(p):  # the last line written for each blank; a last line without its newline was cut by the time-out
    d = {}
    text = p.read_text()
    lines = text.splitlines() if text.endswith("\n") else text.splitlines()[:-1]
    for line in lines:
        if line.strip():
            d[int(line.split()[0])] = line.split()
    return d
bad = 0
def say(ok, what):
    global bad
    print(("ok   " if ok else "FAIL ") + what); bad += not ok
for s in scen:
    roles = ("host",) if s == "offline_demo" else ("host", "join")
    demo = s.startswith("demo") or s == "offline_demo"
    h = {(r, role): blanks(out / f"{r}_{s}_{role}.hash") for r in (A, B) for role in roles}
    for k, v in h.items():
        say(len(v) >= need, f"{s} {k[0]} {k[1]}: {len(v)} blanks (at least {need})")
    for role in roles:
        a, b = h[(A, role)], h[(B, role)]
        common = sorted(set(a) & set(b))
        fight = [t for t in common if a[t][2] != ZERO or b[t][2] != ZERO]
        diff = [t for t in common if a[t][2:] != b[t][2:]]
        whole = sum(1 for t in common if a[t][1] == b[t][1])
        say(not diff and (not demo or len(fight) >= 1000),
            f"{s} {role}: {A} = {B} on the fight's values over {len(common)} blanks, {len(fight)} of them in a fight"
            + (f"; first difference at blank {diff[0]}" if diff else ""))
        print(f"     ({s} {role}: whole-state checksum equal between the two builds on {whole} of {len(common)} blanks)")
    if s == "offline_demo":
        continue
    for r in (A, B):
        a, b = h[(r, "host")], h[(r, "join")]
        common = sorted(set(a) & set(b))
        diff = [t for t in common if a[t][1:] != b[t][1:]]
        fight = sum(1 for t in common if a[t][2] != ZERO)
        say(not diff, f"{s} {r}: host = join, whole state, over {len(common)} blanks ({fight} in a fight)"
            + (f"; first difference at blank {diff[0]}" if diff else ""))
        for role in ("host", "join"):
            log = (out / f"{r}_{s}_{role}.log").read_text(errors="replace")
            say("no longer agree" not in log, f"{s} {r} {role}: no desync reported")
print("RESULT:", "PASS" if not bad else f"FAIL ({bad})")
sys.exit(1 if bad else 0)
EOF
