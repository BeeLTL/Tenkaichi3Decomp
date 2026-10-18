#!/usr/bin/env python3
"""The input table for port/tests/native/net_compare.sh: what each of the two players presses, blank by blank, in the
format BT3_NET_SCRIPT reads (net.c sample_local): per blank 36 bytes, player 1's pad then player 2's, each 18 bytes
as Port_PadRead fills them (two button bytes, active low; then the right stick x, y and the left stick x, y, 0x80 at
rest; the rest zero).

Made up, and the same every time (a fixed seed): idle while the game starts, then both players press buttons and push
the sticks in short bursts, START now and then, so that the two copies of the game have plenty to agree on.

    python3 net_script.py <out file> [blanks]        (default 6000)"""
import random, struct, sys

BUTTONS = [0x0010, 0x0020, 0x0040, 0x0080,  # up right down left
           0x4000, 0x2000, 0x8000, 0x1000,  # cross circle square triangle
           0x0400, 0x0800, 0x0100, 0x0200]  # L1 R1 L2 R2
START = 0x0008


def pad(held, rx=0x80, ry=0x80, lx=0x80, ly=0x80):
    b = ~held & 0xFFFF
    return struct.pack("<BBBBBB12x", b & 0xFF, b >> 8, rx, ry, lx, ly)


def main():
    out, blanks = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 6000
    rnd = random.Random(20261015)
    state = [(0, 0x80, 0x80), (0, 0x80, 0x80)]
    with open(out, "wb") as f:
        for t in range(blanks):
            if t >= 300 and t % 6 == 0:  # a new choice every 6 blanks, after the first 300 (the start-up)
                for p in range(2):
                    held = 0
                    for _ in range(rnd.randint(0, 2)):
                        held |= rnd.choice(BUTTONS)
                    if rnd.random() < 0.04:
                        held |= START
                    lx, ly = rnd.choice([(0x80, 0x80), (0x00, 0x80), (0xFF, 0x80), (0x80, 0x00), (0x80, 0xFF)])
                    state[p] = (held, lx, ly)
            f.write(b"".join(pad(h, lx=lx, ly=ly) for h, lx, ly in state))


if __name__ == "__main__":
    main()
