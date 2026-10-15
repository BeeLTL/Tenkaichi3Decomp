#!/bin/sh
# Runs INSIDE the release container for port/tests/native/net_compare.sh: a checkout at /src, the user's game data at
# /data (read-only), results to /out.
#   sh net_in_container.sh <label> <seconds per scenario>        (NET_SCENARIOS: which to play; default all)
# Builds the Linux x86-64 program (as compare.sh does), then plays each scenario: two copies of it on this machine,
# one hosting (player 1) and one joining (player 2) over UDP on 127.0.0.1, both fed the same input table
# (BT3_NET_SCRIPT), no window (BT3_GS=none), each writing its state checksum every blank (BT3_HASH), each in its own
# folder with the same relative save folder (so that the paths in the game's memory are the same).
set -e
LABEL=$1; SECONDS_EACH=$2
cd /src
export BT3_SKELETON=1
mkdir -p port/build
BT3_CC=clang64 python3 port/tools/undefined.py > port/build/compile_clang64.txt 2>&1 || { tail -5 port/build/compile_clang64.txt; exit 1; }
if grep -q "^FAILED" port/build/compile_clang64.txt; then grep "^FAILED" port/build/compile_clang64.txt | head; exit 1; fi
BT3_CC=clang64 python3 port/tools/link.py > port/build/link_clang64.txt 2>&1 || { cat port/build/link_clang64.txt; exit 1; }
echo "$LABEL clang64: $(tail -1 port/build/link_clang64.txt | cut -c1-120)"
ls port/build/bt3_64.mem >/dev/null 2>&1 && echo "$LABEL: bt3_64.mem present (rollback possible)" || echo "$LABEL: NO bt3_64.mem (rollback scenarios fall back to waiting)"
python3 /native/net_script.py /tmp/net_script.bin 6000
: > /tmp/idle.bin  # (no input at all, for the demo scenarios: START would end the attract demo)

# <name> <port> <environment for both copies...> (it may override BT3_NET_SCRIPT)
scenario() {
    NAME=$1; PORT=$2; shift 2
    rm -rf /tmp/run_$NAME && mkdir -p /tmp/run_$NAME/host /tmp/run_$NAME/join
    for ROLE in host join; do
        if [ $ROLE = host ]; then NET="BT3_NET_HOST=$PORT"; else NET="BT3_NET_JOIN=127.0.0.1:$PORT"; fi
        (cd /tmp/run_$NAME/$ROLE && env BT3_NET_SCRIPT=/tmp/net_script.bin $NET "$@" BT3_DATA=/data BT3_SAVES=saves \
            BT3_GS=none SDL_AUDIODRIVER=dummy BT3_SOUND_TICKS=1 BT3_HASH=/out/${LABEL}_${NAME}_$ROLE.hash \
            timeout "$SECONDS_EACH" /src/port/build/bt3_64 > /out/${LABEL}_${NAME}_$ROLE.log 2>&1 || true) &
        [ $ROLE = host ] && sleep 1
    done
    wait
    echo "$LABEL $NAME: host $(wc -l < /out/${LABEL}_${NAME}_host.hash) blanks, join $(wc -l < /out/${LABEL}_${NAME}_join.hash) blanks"
}
# One copy, no network at all (the attract demo's fight): the control. Two different builds can be compared on the
# fight's values only; their whole-state checksums differ even here, where net.c never runs, because the state holds
# addresses of functions and variables, and those move when any code changes size (state.c).
offline() {
    rm -rf /tmp/run_offline_demo && mkdir -p /tmp/run_offline_demo
    (cd /tmp/run_offline_demo && env BT3_DATA=/data BT3_SAVES=saves BT3_GS=none SDL_AUDIODRIVER=dummy BT3_SOUND_TICKS=1 \
        BT3_DEMO=1 BT3_HASH=/out/${LABEL}_offline_demo_host.hash \
        timeout "$SECONDS_EACH" /src/port/build/bt3_64 > /out/${LABEL}_offline_demo_host.log 2>&1 || true)
    echo "$LABEL offline_demo: $(wc -l < /out/${LABEL}_offline_demo_host.hash) blanks"
}
want() { [ -z "${NET_SCENARIOS:-}" ] || echo " $NET_SCENARIOS " | grep -q " $1 "; }
# upstream's scenarios (docs/netplay_notes.md, stage 4 and rollback), with the made-up input (menus)
want lockstep_d2     && scenario lockstep_d2     47001 BT3_NET_DELAY=2
want loss20_d2       && scenario loss20_d2       47002 BT3_NET_DELAY=2 BT3_NET_LOSS=20
want loss30_lag4_d3  && scenario loss30_lag4_d3  47003 BT3_NET_DELAY=3 BT3_NET_LOSS=30 BT3_NET_LAG=4
want roll6_lat50_l15 && scenario roll6_lat50_l15 47004 BT3_NET_DELAY=2 BT3_NET_ROLLBACK=6 BT3_NET_LATENCY=50 BT3_NET_LOSS=15
# the attract demo's fight, played by two connected copies: fight values to compare between builds
want demo_d2         && scenario demo_d2         47005 BT3_NET_DELAY=2 BT3_DEMO=1 BT3_NET_SCRIPT=/tmp/idle.bin
want demo_roll6      && scenario demo_roll6      47006 BT3_NET_DELAY=2 BT3_NET_ROLLBACK=6 BT3_NET_LATENCY=50 BT3_NET_LOSS=15 BT3_DEMO=1 BT3_NET_SCRIPT=/tmp/idle.bin
want offline_demo    && offline
true
