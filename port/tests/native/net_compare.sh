#!/bin/bash
# Online play, natively: do two revisions behave the same? (docs/BROWSER_ONLINE_PLAN.md, P1: the transport seam.)
#   port/tests/native/net_compare.sh <rev A> <rev B> <gamedata folder> [work folder]
# Needs docker and git; the game data is the user's own (mounted read-only, never copied, never in the image).
# For each revision, in the release container (port/release/Dockerfile, built if missing): net_in_container.sh plays
# upstream's scripted scenarios (two copies over UDP on one machine: lockstep; 20% packet loss; 30% loss with 4 ms of
# lag; rollback 6 with 50 ms latency and 15% loss), each copy writing its state checksum every blank (BT3_HASH).
# Then, per scenario:
#   - A against B, host and joiner each: the checksum lines (the last line of each blank: a blank run again after a
#     rollback is logged again) must be IDENTICAL over the blanks both reached;
#   - in each revision, host against joiner: the same, the two copies agree (as upstream's own test reads it);
#   - no copy may have ended a match with "no longer agree (desync)".
# Exit status 0 only if all of that holds and every run reached MIN_BLANKS. REUSE=1 keeps a revision's results.
set -euo pipefail
A=$1; B=$2; DATA=$3; WORK=${4:-$(mktemp -d)}
SECONDS_EACH=${SECONDS_EACH:-120}; MIN_BLANKS=${MIN_BLANKS:-1500}
HERE=$(cd "$(dirname "$0")" && pwd); REPO=$(cd "$HERE/../../.." && pwd)
IMAGE=bt3-port-release:jammy
docker info >/dev/null 2>&1 || { echo "Docker is not running (start Docker Desktop): this check needs it" >&2; exit 2; }
docker image inspect $IMAGE >/dev/null 2>&1 || docker build -t $IMAGE "$REPO/port/release"
mkdir -p "$WORK/out"
host() { if command -v cygpath >/dev/null; then cygpath -w "$1"; else echo "$1"; fi; }
SCENARIOS="lockstep_d2 loss20_d2 loss30_lag4_d3 roll6_lat50_l15"
for rev in "$A" "$B"; do
    if [ "${REUSE:-0}" = 1 ] && [ -s "$WORK/out/${rev}_roll6_lat50_l15_join.hash" ]; then echo "$rev: results reused"; continue; fi
    src="$WORK/src_$rev"
    rm -rf "$src" && git -c core.autocrlf=false clone -q "$REPO" "$src" && git -C "$src" checkout -q "$rev"
    MSYS_NO_PATHCONV=1 docker run --rm -v "$(host "$src"):/src" -v "$(host "$DATA"):/data:ro" -v "$(host "$WORK/out"):/out" \
        -v "$(host "$HERE"):/native:ro" $IMAGE sh /native/net_in_container.sh "$rev" "$SECONDS_EACH"
done
python3 - "$WORK/out" "$A" "$B" "$MIN_BLANKS" $SCENARIOS <<'EOF'
import sys, pathlib
out, A, B, need, scen = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3], int(sys.argv[4]), sys.argv[5:]
def blanks(p):  # the last line written for each blank
    d = {}
    for line in p.read_text().splitlines():
        if line.strip():
            d[int(line.split()[0])] = line
    return d
bad = 0
def say(ok, what):
    global bad
    print(("ok   " if ok else "FAIL ") + what); bad += not ok
for s in scen:
    h = {(r, role): blanks(out / f"{r}_{s}_{role}.hash") for r in (A, B) for role in ("host", "join")}
    for k, v in h.items():
        say(len(v) >= need, f"{s} {k[0]} {k[1]}: {len(v)} blanks (at least {need})")
    for role in ("host", "join"):
        a, b = h[(A, role)], h[(B, role)]
        common = sorted(set(a) & set(b))
        diff = [t for t in common if a[t] != b[t]]
        say(not diff and len(common) >= need, f"{s} {role}: {A} = {B} over {len(common)} blanks" + (f"; first difference at blank {diff[0]}" if diff else ""))
    for r in (A, B):
        a, b = h[(r, "host")], h[(r, "join")]
        common = sorted(set(a) & set(b))
        diff = [t for t in common if a[t].split()[1:] != b[t].split()[1:]]
        say(not diff, f"{s} {r}: host = join over {len(common)} blanks" + (f"; first difference at blank {diff[0]}" if diff else ""))
        for role in ("host", "join"):
            log = (out / f"{r}_{s}_{role}.log").read_text(errors="replace")
            say("no longer agree" not in log, f"{s} {r} {role}: no desync reported")
print("RESULT:", "PASS" if not bad else f"FAIL ({bad})")
sys.exit(1 if bad else 0)
EOF
