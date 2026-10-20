#!/usr/bin/env python3
"""Listening tests for the velocity table.

Plays notes through the controller as it plays them from a velocity table
(the duty, coast and hold for each key and velocity; see PianoController::
sendPress), so you can judge by ear what the measurements say. Each note is
printed with the command sent, so you can note which one sounded wrong.
Ctrl-C stops and releases every key.

  scale       every key at one velocity: do any stick out?
  neighbours  a key and its neighbours in turn: is a difference real?
              (--duty plays them all at one raw duty instead of a velocity)
  crescendo   one key from velocity 1 to 127: are the steps even?
  softest     every key at velocity 1, a few times: does any fail to sound?
  loudest     every key at velocity 127: even, and no double strikes?

Settings live in the SETTINGS block below; any of them can be overridden for
one run on the command line (see --help on each command). The connection
(CONNECTION, SERIAL_PORT, HOST) comes from calibration_common.py's settings.

How each note is driven comes from the table's "drive" and each key's own
settings (calibrate_dynamics.py's STRIKE_MS, COAST_OFFSET_MS, CLAMP_MS, ...).
To try other values without rebuilding the table, --coast-offset-ms,
--clamp-offset-ms, --clamp-ms, --clamp-duty and --hold override them for one
run; with --above, only on strikes above that duty (the table's below).

Examples:
  python3 tools/listen.py scale
  python3 tools/listen.py crescendo 30 --clamp-ms 40 --clamp-offset-ms -15 --above 40
  python3 tools/listen.py neighbours 62 --duty 60
  python3 tools/listen.py crescendo 30 --pedal --step 4   # soft notes with the dampers off
  python3 tools/listen.py scale --table data/alt.json   # a table made with `table --out data/alt.json`
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import calibration_common as common  # noqa: E402  (connection, settings, key names)

# === SETTINGS ===============================================================

TABLE = common.TABLE_FILE  # the velocity table to play from
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

# The sustain pedal, so the dampers can't hide a bounce that strikes twice:
# None, "note" (pressed before each note and released after it, so no note
# rings into the next: --pedal) or "held" (down through the whole test:
# --pedal-held).
PEDAL = None
PEDAL_RING_MS = 400  # with "note", how long a note rings after its key is let go, before the pedal lifts

# How long each note is held and the silence after it, per test, in ms.
TIMING_MS = {
    "scale": (200, 250),
    "neighbours": (200, 250),
    "crescendo": (250, 450),
    "softest": (200, 250),
    "loudest": (200, 250),
}

# ============================================================================

# The sustain pedal, pressed as a piece presses it (with the commands in the
# controller's config). Stopping at the end releases it, with every key.
PEDAL_CHANNELS = common.PEDAL_CHANNELS
PEDAL_SETTLE_S = 0.25  # for the pedal to go down
PEDAL_LIFT_S = 0.15  # for it to come up

# The firmware's default mapping for keys the table doesn't cover
# (DEFAULT_MIN_DUTY / DEFAULT_MAX_DUTY in include/app/piano/velocity_table.h).
DEFAULT_MIN_DUTY = 35
DEFAULT_MAX_DUTY = 80


class Table:
    def __init__(self, path: Path):
        if not path.exists():
            sys.exit(f"{path} not found: run `calibrate_dynamics.py table` first")
        document = json.loads(path.read_text())
        self.entries = {int(k): v for k, v in document["keys"].items()}
        self.keys = {k: v["duty"] for k, v in self.entries.items()}
        self.shared_landing = document.get("landing_ms")
        # Tables made before they carried it are played as the controller would: with its defaults.
        self.drive = {**common.DEFAULT_DRIVE, **document.get("drive", {})}
        self.overrides: dict = {}  # key entry field -> value, set from the command line
        self.above: float | None = (
            None  # the overrides apply only to strikes above this duty
        )
        self.settings = document.get("settings", {})
        # With the pedal down, keys with a "pedal" entry play from it, as the controller plays them.
        self.pedal = False
        landing = (
            "with landing times"
            if self.shared_landing
            or any("landing_ms" in v for v in self.entries.values())
            else "no landing times, so plain presses"
        )
        print(
            f"Table {path.name} ({len(self.keys)} keys, {landing}; settings {self.settings})"
        )

    def entry(self, key: int) -> dict:
        """The key's table entry, or with the pedal down its "pedal" entry if it has one."""
        entry = self.entries.get(key, {})
        return entry["pedal"] if self.pedal and "pedal" in entry else entry

    def duty(self, key: int, velocity: int) -> float:
        velocity = min(max(velocity, 1), 127)
        if key in self.keys:
            return self.entry(key)["duty"][velocity - 1]
        # As the firmware works it out, in hundredths of a percent.
        hundredths = (
            DEFAULT_MIN_DUTY * 100
            + (velocity - 1) * (DEFAULT_MAX_DUTY - DEFAULT_MIN_DUTY) * 100 // 126
        )
        return hundredths / 100

    def command(self, key: int, velocity: int) -> str:
        """The press the controller sends for this key and velocity, with any
        command-line overrides."""
        velocity = min(max(velocity, 1), 127)
        entry = dict(self.entry(key))
        drive = self.drive
        for name, value in self.overrides.items():
            if self.above is None:
                entry[name] = value
                continue
            # Keep what the table gives up to `above`, then the override.
            current = entry.get(name, self.drive[common.KEY_DRIVE_FIELDS[name]])
            steps = current if isinstance(current, list) else [[0, current]]
            entry[name] = [step for step in steps if step[0] < self.above] + [
                [self.above, value]
            ]
        landing_list = entry.get("landing_ms") or self.shared_landing  # a pedal entry always has its own
        landing = landing_list[velocity - 1] if landing_list else None
        return common.drive_command(
            key, self.duty(key, velocity), landing, drive, entry
        )


format_duty = common.format_duty


class Player:
    def __init__(self, args):
        self.piano = common.connect(args)
        self.hold_s = args.hold_ms / 1000
        self.gap_s = args.gap_ms / 1000
        self.count = 0
        self.pedal = args.pedal
        self.ring_s = args.pedal_ring_ms / 1000
        if self.pedal == "held":
            print("Sustain pedal down through the test.")
            self.set_pedal(True)
        elif self.pedal == "note":
            print("Sustain pedal down for each note.")

    def set_pedal(self, down: bool) -> None:
        command = self.piano.config()["pedal_press" if down else "pedal_release"]
        for channel in PEDAL_CHANNELS:
            self.piano.send(f"P:{channel}:{command}")
        time.sleep(PEDAL_SETTLE_S if down else PEDAL_LIFT_S)

    def play(
        self, key: int, duty: float, label: str, command: str | None = None
    ) -> None:
        """Presses the key with `command`, or plainly at `duty` without one."""
        command = command or f"P:{key}:{format_duty(duty)}"
        self.count += 1
        print(
            f"  {self.count:3d}. key {key:2d} ({common.note_name(key):>3s})  {label:14s} duty {format_duty(duty):>6s}%  {command}"
        )
        if self.pedal == "note":
            self.set_pedal(True)
        self.piano.send(command)
        time.sleep(self.hold_s)
        self.piano.send(f"R:{key}:0")
        if self.pedal == "note":
            time.sleep(self.ring_s)
            self.set_pedal(False)
        time.sleep(self.gap_s)

    def pause(self, seconds: float) -> None:
        time.sleep(seconds)


def keys_in(args, table: Table) -> list[int]:
    keys = common.parse_keys(args.keys)
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
        player.play(
            key,
            table.duty(key, args.velocity),
            f"velocity {args.velocity}",
            table.command(key, args.velocity),
        )


def neighbours(args, table: Table, player: Player) -> None:
    keys = [
        k
        for k in range(args.key - args.span, args.key + args.span + 1)
        if 0 <= k < common.PIANO_KEYS
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
                    key,
                    table.duty(key, args.velocity),
                    f"velocity {args.velocity}",
                    table.command(key, args.velocity),
                )
        player.pause(0.6)


def crescendo(args, table: Table, player: Player) -> None:
    velocities = list(range(1, 128, args.step))
    if velocities[-1] != 127:
        velocities.append(127)
    if args.down:
        velocities += velocities[-2::-1]
    print(
        f"\nKey {args.key} ({common.note_name(args.key)}) from velocity 1 to 127{' and back' if args.down else ''}. "
        "Each step should sound a similar amount louder, with no jump at the soft end and no plateau at the top."
    )
    for velocity in velocities:
        player.play(
            args.key,
            table.duty(args.key, velocity),
            f"velocity {velocity}",
            table.command(args.key, velocity),
        )


def softest(args, table: Table, player: Player) -> None:
    print(
        f"\nEvery key at velocity 1, {args.repeats} times each. Note any strike that doesn't sound: "
        "that key's min duty is too low (raise MIN_MARGIN, or measure it again)."
    )
    for key in keys_in(args, table):
        for _ in range(args.repeats):
            player.play(key, table.duty(key, 1), "velocity 1", table.command(key, 1))
        player.pause(0.3)


def loudest(args, table: Table, player: Player) -> None:
    print(
        "\nEvery key at velocity 127. Listen for keys that stick out, and for double strikes "
        "(the hammer hitting the string twice)."
    )
    for key in keys_in(args, table):
        player.play(key, table.duty(key, 127), "velocity 127", table.command(key, 127))


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    commands = parser.add_subparsers(dest="command", required=True)

    def add_common(p, test):
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
        p.add_argument(
            "--coast-offset-ms",
            type=float,
            help="when the hold starts without a clamp, from the landing",
        )
        p.add_argument(
            "--clamp-offset-ms",
            type=float,
            help="when the clamp starts, from the landing",
        )
        p.add_argument("--clamp-ms", type=float, help="clamp length (0 = no clamp)")
        p.add_argument("--clamp-duty", type=float, help="clamp duty")
        p.add_argument("--hold", type=float, help="hold duty")
        p.add_argument(
            "--pedal",
            action="store_const",
            const="note",
            default=PEDAL,
            help="press the sustain pedal for each note (released after it rings), so the dampers can't hide "
            "a bounce that strikes twice",
        )
        p.add_argument(
            "--pedal-held",
            dest="pedal",
            action="store_const",
            const="held",
            help="keep the sustain pedal down through the whole test instead",
        )
        p.add_argument(
            "--pedal-ring-ms",
            type=int,
            default=PEDAL_RING_MS,
            help="with --pedal, how long each note rings after its key is let go",
        )
        p.add_argument(
            "--above",
            type=float,
            help="apply the overrides above only to strikes above this duty",
        )
        p.add_argument(
            "--connection", choices=["usb", "wifi"], default=common.CONNECTION
        )
        p.add_argument("--port", default=common.SERIAL_PORT)
        p.add_argument("--host", default=common.HOST)

    def key_range(p):
        p.add_argument("--keys", default=KEYS, help="e.g. 0-87, 36-60 or 30-40,50")
        p.add_argument(
            "--include-uncalibrated",
            action="store_true",
            default=INCLUDE_UNCALIBRATED,
            help="also play keys not in the table",
        )

    p = commands.add_parser("scale", help="every key at one velocity")
    add_common(p, "scale")
    key_range(p)
    p.add_argument("--velocity", type=int, default=VELOCITY)
    p.set_defaults(run=scale)

    p = commands.add_parser("neighbours", help="a key and its neighbours in turn")
    add_common(p, "neighbours")
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
    add_common(p, "crescendo")
    p.add_argument("key", type=int, nargs="?", default=CRESCENDO_KEY)
    p.add_argument("--step", type=int, default=CRESCENDO_STEP, help="velocity step")
    p.add_argument(
        "--down", action="store_true", default=CRESCENDO_DOWN, help="come back down too"
    )
    p.set_defaults(run=crescendo)

    p = commands.add_parser("softest", help="every key at velocity 1, a few times")
    add_common(p, "softest")
    key_range(p)
    p.add_argument("--repeats", type=int, default=SOFTEST_REPEATS)
    p.set_defaults(run=softest)

    p = commands.add_parser("loudest", help="every key at velocity 127")
    add_common(p, "loudest")
    key_range(p)
    p.set_defaults(run=loudest)

    args = parser.parse_args()
    for name in ("key",):
        if hasattr(args, name) and not 0 <= getattr(args, name) < common.PIANO_KEYS:
            sys.exit("key must be 0-87")
    if getattr(args, "duty", None) is not None and not 0 <= args.duty <= 100:
        sys.exit("duty must be 0-100")
    if hasattr(args, "velocity") and not 1 <= args.velocity <= 127:
        sys.exit("velocity must be 1-127")

    table = Table(args.table)
    table.pedal = args.pedal is not None
    if table.pedal:
        with_pedal = sorted(k for k, v in table.entries.items() if "pedal" in v)
        print(f"Pedal down: {len(with_pedal)} keys play from their pedal entries"
              + (f" ({', '.join(map(str, with_pedal))})" if with_pedal else "")
              + "; the rest as with the pedal up")
    table.overrides = {
        name: value
        for name, value in (
            ("coast_offset_ms", args.coast_offset_ms),
            ("clamp_offset_ms", args.clamp_offset_ms),
            ("clamp_ms", args.clamp_ms),
            ("clamp_duty", args.clamp_duty),
            ("hold", args.hold),
        )
        if value is not None
    }
    table.above = args.above
    print(
        f"Drive: {table.drive}"
        + (f"; overridden: {table.overrides}" if table.overrides else "")
        + (
            f" above {args.above}%"
            if table.overrides and args.above is not None
            else ""
        )
    )
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
