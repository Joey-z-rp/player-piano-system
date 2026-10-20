#!/usr/bin/env python3
"""Does the sustain pedal change how a key plays? With the pedal down the
dampers are already lifted, so a key doesn't lift its own: it's lighter, and
may play louder, land sooner, or bounce where it doesn't with the pedal up.
Measured with the microphone, each test key struck with the pedal up and down.

  measure  strike each test key with the pedal up and down (saves after each
           key; --redo to measure again)
  report   print the comparison

The connection and microphone settings are in calibration_common.py.
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import calibration_common as common  # noqa: E402

# === SETTINGS ===============================================================

# Each test key, at the velocity table's duty for each of VELOCITIES, struck
# STRIKES times each way, interleaved:
#   long   the long strike, so the landing is the hammer's own
#   table  the press the controller sends, from the velocity table (its clamp
#          or hold timed to the pedal-up landing), for the bounce check
KEYS = "20,40,60"
VELOCITIES = [1, 8, 16, 24, 32, 48, 64, 96]
STRIKES = 3
LONG_STRIKE_MS = 250  # the long strike: long enough that the hammer always lands first
PRESS_MS = 400  # how long each key is held before the release
RAW_FILE = common.PROJECT_DIR / "calibration" / "pedal_tests.json"

# ============================================================================


def pedal_key(key: int, table: dict, ctx) -> list[dict]:
    rows: list[dict] = []
    rate = ctx.args.sample_rate
    duties = table["keys"][str(key)]["duty"]
    print(f"\n== key {key} ({common.note_name(key)}) ==")

    def strike(velocity: int, way: str, pedal: bool) -> dict:
        duty = duties[velocity - 1]
        press = (f"P:{key}:{common.format_duty(duty)}:{LONG_STRIKE_MS}:0:0:0" if way == "long"
                 else common.table_command(key, velocity, table))
        while True:
            waited, quiet = common.wait_until_quiet(ctx.recorder, ctx.sos, ctx.room_db, common.QUIET_DB,
                                                    common.MAX_QUIET_WAIT_S)
            if pedal:
                common.set_pedal(ctx.piano, True)
            try:
                recorded = common.record_press(ctx.timed, ctx.piano, press, f"R:{key}:0", PRESS_MS)
            finally:
                if pedal:
                    common.set_pedal(ctx.piano, False)
            if recorded is None:
                print("  network hiccup, doing it again")
                continue
            audio, _ = recorded
            measured = common.analyse_strike(audio, rate, ctx.sos, ctx.args.margin_db, key)
            if quiet and measured.noise_db > ctx.room_db + common.QUIET_DB + 3:
                print(f"  background too loud ({measured.noise_db:.1f} dB), doing it again")
                continue
            break
        hit = measured.result == "hit"
        row = {"key": key, "velocity": velocity, "duty": duty, "way": way, "pedal": pedal,
               "result": measured.result, "peak_db": round(measured.peak_db - ctx.args.gain_db, 2),
               "lands_ms": measured.lands_ms if hit else None, "bounce": measured.bounce, "command": press}
        lands = f"lands {measured.lands_ms:4d} ms" if hit and measured.lands_ms is not None else ""
        print(f"  v{velocity:<3d} {common.format_duty(duty):>6s}%  {way:5s} pedal {'down' if pedal else 'up  '}  "
              f"{measured.result:9s} {measured.peak_db:6.1f} dB  {lands:15s}{'  BOUNCE' if measured.bounce else ''}")
        return row

    # Interleaved, so slow drift (temperature, the room) affects every way alike.
    for velocity in VELOCITIES:
        for _ in range(STRIKES):
            for way in ("long", "table"):
                for pedal in (False, True):
                    rows.append(strike(velocity, way, pedal))
    return rows


def measure(args) -> None:
    table = json.loads(common.TABLE_FILE.read_text())
    done = {r["key"] for r in load(args.raw)}
    keys = [k for k in common.parse_keys(args.keys) if args.redo or k not in done]
    missing = [k for k in keys if str(k) not in table["keys"]]
    if missing:
        sys.exit(f"keys {missing} aren't in {common.TABLE_FILE}")
    if not keys:
        print(f"Every key asked for is already in {args.raw} (use --redo to measure it again)")
        return
    strikes = len(keys) * len(VELOCITIES) * STRIKES * 4
    print(f"{strikes} strikes on {len(keys)} key(s); roughly {strikes * 3.5 / 60:.0f} minutes")
    ctx = common.Session(args)
    try:
        for key in keys:
            rows = pedal_key(key, table, ctx)
            kept = [r for r in load(args.raw) if r["key"] != key]
            args.raw.parent.mkdir(parents=True, exist_ok=True)
            temp = args.raw.with_suffix(".tmp")
            temp.write_text('{"pedal": [\n' + ",\n".join(json.dumps(r) for r in kept + rows) + "\n]}\n")
            temp.replace(args.raw)
    finally:
        ctx.piano.release_everything()


def load(path: Path) -> list[dict]:
    if not path.exists():
        return []
    return json.loads(path.read_text())["pedal"]


def report(args) -> None:
    rows = load(args.raw)
    if not rows:
        sys.exit(f"no measurements in {args.raw}: run `pedal` first")
    grouped = defaultdict(list)
    for r in rows:
        grouped[(r["key"], r["velocity"], r["way"], r["pedal"])].append(r)

    def stats(key, velocity, way, down):
        group = grouped.get((key, velocity, way, down), [])
        hits = [r for r in group if r["result"] == "hit"]
        lands = [r["lands_ms"] for r in hits if r["lands_ms"] is not None]
        return {"strikes": len(group), "hits": len(hits),
                "peak": statistics.median(r["peak_db"] for r in hits) if hits else None,
                "lands": statistics.median(lands) if lands else None,
                "bounces": sum(r["bounce"] for r in hits)}

    def shift(a, b, unit):
        return f"{b - a:+6.1f} {unit}" if a is not None and b is not None else f"{'-':>9s}"

    for key in sorted({r["key"] for r in rows}):
        print(f"\nkey {key} ({common.note_name(key)})   pedal up -> down")
        print(f"  {'vel':>3s} {'duty':>6s}   {'lands (long)':>18s} {'shift':>9s}   {'level (long)':>17s} {'diff':>9s}"
              f"   {'bounces (controller press)':>26s}")
        for velocity in sorted({r["velocity"] for r in rows if r["key"] == key}):
            duty = grouped[next(k for k in grouped if k[0] == key and k[1] == velocity)][0]["duty"]
            up, down = stats(key, velocity, "long", False), stats(key, velocity, "long", True)
            t_up, t_down = stats(key, velocity, "table", False), stats(key, velocity, "table", True)
            lands = (f"{up['lands'] or 0:5.0f} -> {down['lands'] or 0:5.0f} ms" if up["lands"] and down["lands"]
                     else f"{'-':>18s}")
            level = (f"{up['peak']:6.1f} -> {down['peak']:6.1f}" if up["peak"] is not None and down["peak"] is not None
                     else f"{'played ' + str(up['hits']) + '/' + str(down['hits']):>17s}")
            bounces = f"up {t_up['bounces']}/{t_up['hits']}, down {t_down['bounces']}/{t_down['hits']}"
            print(f"  {velocity:3d} {common.format_duty(duty):>5s}%   {lands:>18s} {shift(up['lands'], down['lands'], 'ms')}"
                  f"   {level:>17s} {shift(up['peak'], down['peak'], 'dB')}   {bounces:>26s}")
    print("\nlands: when the hammer hits the string on the long strike (median), and how much sooner (-) or later "
          "with the pedal down. level: its peak, dB. bounces: of the strikes that played with the controller's own "
          "press, how many bounced.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)

    p = commands.add_parser("measure", help="strike test keys with the sustain pedal up and down")
    p.add_argument("--keys", default=KEYS)
    p.add_argument("--redo", action="store_true", help="measure again, replacing earlier results")
    p.add_argument("--raw", type=Path, default=RAW_FILE)
    common.add_session_args(p)
    p.set_defaults(run=measure)

    p = commands.add_parser("report", help="print the pedal comparison")
    p.add_argument("--raw", type=Path, default=RAW_FILE)
    p.set_defaults(run=report)

    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
