#!/usr/bin/env python3
"""Listening tests for the velocity table.

Plays notes through the controller with the duties from a velocity table, so
you can judge by ear what the measurements say. Each note is printed as it
plays, so you can note which one sounded wrong. Ctrl-C stops and releases
every key.

  scale       every key at one velocity: do any stick out?
  neighbours  a key and its neighbours in turn: is a difference real?
              (--duty plays them all at one raw duty instead of a velocity)
  crescendo   one key from velocity 1 to 127: are the steps even?
  softest     every key at velocity 1, a few times: does any fail to sound?
  loudest     every key at velocity 127: even, and no double strikes?

Settings live in the SETTINGS block below; any of them can be overridden for
one run on the command line (see --help on each command). The connection
(CONNECTION, SERIAL_PORT, HOST) comes from calibrate_dynamics.py's settings.

Examples:
  python3 tools/listen.py scale
  python3 tools/listen.py neighbours 62 --duty 60
  python3 tools/listen.py scale --table data/alt.json   # a table made with `table --out data/alt.json`
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import calibrate_dynamics as cal  # noqa: E402  (connection, settings, key names)

# === SETTINGS ===============================================================

TABLE = cal.TABLE_FILE  # the velocity table to play from
KEYS = "0-87"  # keys scale, softest and loudest go through, e.g. "36-60" or "30-40,50"
INCLUDE_UNCALIBRATED = (
    False  # also play keys not in the table, with the default mapping
)
VELOCITY = 64  # velocity for scale and neighbours

NEIGHBOURS_KEY = 62  # the key neighbours centres on
NEIGHBOURS_SPAN = 1  # keys on each side of it
NEIGHBOURS_ROUNDS = 3
NEIGHBOURS_DUTY = None  # a raw duty (e.g. 60) to check the microphone; None plays VELOCITY from the table

CRESCENDO_KEY = 40
CRESCENDO_STEP = 8  # velocity step
CRESCENDO_DOWN = False  # come back down too

SOFTEST_REPEATS = 3  # strikes per key at velocity 1

# How long each note is held and the silence after it, per test, in ms.
TIMING_MS = {
    "scale": (200, 250),
    "neighbours": (200, 250),
    "crescendo": (200, 250),
    "softest": (200, 250),
    "loudest": (200, 250),
}

# ============================================================================

# The firmware's default mapping for keys the table doesn't cover
# (DEFAULT_MIN_DUTY / DEFAULT_MAX_DUTY in include/app/piano/velocity_table.h).
DEFAULT_MIN_DUTY = 35
DEFAULT_MAX_DUTY = 80


class Table:
    def __init__(self, path: Path):
        if not path.exists():
            sys.exit(f"{path} not found: run `calibrate_dynamics.py table` first")
        document = json.loads(path.read_text())
        self.keys = {int(k): v["duty"] for k, v in document["keys"].items()}
        self.settings = document.get("settings", {})
        print(f"Table {path.name} ({len(self.keys)} keys; settings {self.settings})")

    def duty(self, key: int, velocity: int) -> float:
        velocity = min(max(velocity, 1), 127)
        if key in self.keys:
            return self.keys[key][velocity - 1]
        # As the firmware works it out, in hundredths of a percent.
        hundredths = DEFAULT_MIN_DUTY * 100 + (velocity - 1) * (DEFAULT_MAX_DUTY - DEFAULT_MIN_DUTY) * 100 // 126
        return hundredths / 100


def format_duty(duty: float) -> str:
    """As the driver takes it: percent with up to two decimals, e.g. 32, 32.5, 32.25."""
    return f"{duty:.2f}".rstrip("0").rstrip(".")


class Player:
    def __init__(self, args):
        self.piano = cal.connect(args)
        self.hold_s = args.hold_ms / 1000
        self.gap_s = args.gap_ms / 1000
        self.count = 0

    def play(self, key: int, duty: float, label: str) -> None:
        self.count += 1
        print(
            f"  {self.count:3d}. key {key:2d} ({cal.note_name(key):>3s})  {label:14s} duty {format_duty(duty):>6s}%"
        )
        self.piano.send(f"P:{key}:{format_duty(duty)}")
        time.sleep(self.hold_s)
        self.piano.send(f"R:{key}:0")
        time.sleep(self.gap_s)

    def pause(self, seconds: float) -> None:
        time.sleep(seconds)


def keys_in(args, table: Table) -> list[int]:
    keys = cal.parse_keys(args.keys)
    skipped = [k for k in keys if k not in table.keys]
    if skipped and not args.include_uncalibrated:
        print(
            f"Skipping keys not in the table: {skipped} (--include-uncalibrated plays them with the default mapping)"
        )
        keys = [k for k in keys if k in table.keys]
    return keys


def scale(args, table: Table, player: Player) -> None:
    print(
        f"\nEvery key at velocity {args.velocity}. Listen for keys that stick out, too loud or too soft."
    )
    for key in keys_in(args, table):
        player.play(key, table.duty(key, args.velocity), f"velocity {args.velocity}")


def neighbours(args, table: Table, player: Player) -> None:
    keys = [
        k
        for k in range(args.key - args.span, args.key + args.span + 1)
        if 0 <= k < cal.PIANO_KEYS
    ]
    if args.duty is not None:
        print(
            f"\nKeys {keys[0]}-{keys[-1]} in turn at a raw duty of {args.duty}%. A key that sounds louder or "
            "softer here really is; if they sound alike, the microphone exaggerated the difference."
        )
    else:
        print(
            f"\nKeys {keys[0]}-{keys[-1]} in turn at velocity {args.velocity}. With the table they should sound alike."
        )
    for round_number in range(1, args.rounds + 1):
        print(f" round {round_number}")
        for key in keys:
            if args.duty is not None:
                player.play(key, args.duty, "raw duty")
            else:
                player.play(
                    key, table.duty(key, args.velocity), f"velocity {args.velocity}"
                )
        player.pause(0.6)


def crescendo(args, table: Table, player: Player) -> None:
    velocities = list(range(1, 128, args.step))
    if velocities[-1] != 127:
        velocities.append(127)
    if args.down:
        velocities += velocities[-2::-1]
    print(
        f"\nKey {args.key} ({cal.note_name(args.key)}) from velocity 1 to 127{' and back' if args.down else ''}. "
        "Each step should sound a similar amount louder, with no jump at the soft end and no plateau at the top."
    )
    for velocity in velocities:
        player.play(args.key, table.duty(args.key, velocity), f"velocity {velocity}")


def softest(args, table: Table, player: Player) -> None:
    print(
        f"\nEvery key at velocity 1, {args.repeats} times each. Note any strike that doesn't sound: "
        "that key's min duty is too low (raise MIN_MARGIN, or measure it again)."
    )
    for key in keys_in(args, table):
        for _ in range(args.repeats):
            player.play(key, table.duty(key, 1), "velocity 1")
        player.pause(0.3)


def loudest(args, table: Table, player: Player) -> None:
    print(
        "\nEvery key at velocity 127. Listen for keys that stick out, and for double strikes "
        "(the hammer hitting the string twice)."
    )
    for key in keys_in(args, table):
        player.play(key, table.duty(key, 127), "velocity 127")


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    commands = parser.add_subparsers(dest="command", required=True)

    def common(p, test):
        hold_ms, gap_ms = TIMING_MS[test]
        p.add_argument(
            "--table", type=Path, default=TABLE, help="velocity table to play from"
        )
        p.add_argument(
            "--hold-ms", type=int, default=hold_ms, help="how long each note is held"
        )
        p.add_argument(
            "--gap-ms", type=int, default=gap_ms, help="silence after each note"
        )
        p.add_argument("--connection", choices=["usb", "wifi"], default=cal.CONNECTION)
        p.add_argument("--port", default=cal.SERIAL_PORT)
        p.add_argument("--host", default=cal.HOST)

    def key_range(p):
        p.add_argument("--keys", default=KEYS, help="e.g. 0-87, 36-60 or 30-40,50")
        p.add_argument(
            "--include-uncalibrated",
            action="store_true",
            default=INCLUDE_UNCALIBRATED,
            help="also play keys not in the table",
        )

    p = commands.add_parser("scale", help="every key at one velocity")
    common(p, "scale")
    key_range(p)
    p.add_argument("--velocity", type=int, default=VELOCITY)
    p.set_defaults(run=scale)

    p = commands.add_parser("neighbours", help="a key and its neighbours in turn")
    common(p, "neighbours")
    p.add_argument("key", type=int, nargs="?", default=NEIGHBOURS_KEY)
    p.add_argument(
        "--span", type=int, default=NEIGHBOURS_SPAN, help="neighbours on each side"
    )
    p.add_argument("--velocity", type=int, default=VELOCITY)
    p.add_argument(
        "--duty",
        type=float,
        default=NEIGHBOURS_DUTY,
        help="play a raw duty instead of a velocity, to check the microphone",
    )
    p.add_argument("--rounds", type=int, default=NEIGHBOURS_ROUNDS)
    p.set_defaults(run=neighbours)

    p = commands.add_parser("crescendo", help="one key from velocity 1 to 127")
    common(p, "crescendo")
    p.add_argument("key", type=int, nargs="?", default=CRESCENDO_KEY)
    p.add_argument("--step", type=int, default=CRESCENDO_STEP, help="velocity step")
    p.add_argument(
        "--down", action="store_true", default=CRESCENDO_DOWN, help="come back down too"
    )
    p.set_defaults(run=crescendo)

    p = commands.add_parser("softest", help="every key at velocity 1, a few times")
    common(p, "softest")
    key_range(p)
    p.add_argument("--repeats", type=int, default=SOFTEST_REPEATS)
    p.set_defaults(run=softest)

    p = commands.add_parser("loudest", help="every key at velocity 127")
    common(p, "loudest")
    key_range(p)
    p.set_defaults(run=loudest)

    args = parser.parse_args()
    for name in ("key",):
        if hasattr(args, name) and not 0 <= getattr(args, name) < cal.PIANO_KEYS:
            sys.exit("key must be 0-87")
    if getattr(args, "duty", None) is not None and not 0 <= args.duty <= 100:
        sys.exit("duty must be 0-100")
    if hasattr(args, "velocity") and not 1 <= args.velocity <= 127:
        sys.exit("velocity must be 1-127")

    table = Table(args.table)
    player = Player(args)
    try:
        args.run(args, table, player)
        print("\nDone.")
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        player.piano.release_everything()


if __name__ == "__main__":
    main()
