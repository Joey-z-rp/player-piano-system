#!/usr/bin/env python3
"""Tests for choosing the strike length and hold, measured with the microphone.

The duty drive pushes at the strike duty for STRIKE_MS, then drops to the
hold. If a soft strike's hammer is still in flight when the strike ends, the
hold keeps pushing it and changes how loud it plays; if a loud strike's
hammer lands well before then, full power keeps driving a key that has
already bottomed out. These tests find out when the hammer actually lands.

  flight   strike each test key at a spread of duties, three ways, timing when
           the hammer lands (saves after each key; rerun to resume). --duties
           picks the duties, e.g. --duties 18,20,22; --variants picks the ways
  report   print what was found and write calibration/hold_tests.html
  pedal    strike each test key with the sustain pedal up and down, to see
           whether the pedal changes when the hammer lands, how loud it
           plays, or whether it bounces (saves after each key; --redo)
  pedal-report  print the pedal comparison

The three ways (VARIANTS below):
  long       the strike lasts LONG_STRIKE_MS, with no hold, so it never ends
             before the hammer lands: when the hammer lands on its own
  50 + hold  today's drive: STRIKE_MS, then HOLD_DUTY
  50 only    STRIKE_MS with no hold: whatever the hold adds is missing here

The connection and microphone settings are in calibration_common.py.
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
import time
from collections import defaultdict
from pathlib import Path

import numpy as np
from scipy import signal

sys.path.insert(0, str(Path(__file__).resolve().parent))
import calibration_common as common  # noqa: E402

# === SETTINGS ===============================================================

KEYS = "40"  # test keys, e.g. "10,40,70"
# Duties tried on each key: the velocity table's duty at these velocities,
# so they cover the key's playable range.
VELOCITIES = [1, 8, 16, 32, 48, 64, 96, 127]
STRIKES = 3  # strikes per duty and variant

STRIKE_MS = 50  # today's strike length
HOLD_DUTY = 20  # today's hold, %
LONG_STRIKE_MS = 250  # long enough that the hammer always lands first
PRESS_MS = 400  # how long each key is held before the release

# Landing detection settings are in calibration_common.py.
ENVELOPE_MS = (-20, 350)  # stretch of each strike's level kept for the report

# The pedal comparison (pedal). With the pedal down, the dampers are already
# lifted, so the key doesn't lift its own: it's lighter, and may play louder,
# land sooner, or bounce where it doesn't with the pedal up. Each duty is
# struck PEDAL_STRIKES times each way, interleaved:
#   long   the long strike, so the landing is the hammer's own
#   table  the press the controller sends, from the velocity table (its clamp
#          or hold timed to the pedal-up landing), for the bounce check
PEDAL_KEYS = "20,40,60"
PEDAL_VELOCITIES = [1, 8, 16, 24, 32, 48, 64, 96]
PEDAL_STRIKES = 3
PEDAL_CHANNELS = (88, 89)  # as the controller presses it (SUSTAIN_PEDAL_* in piano_controller.h)
PEDAL_PRESS = "100:30:100:30:55"
PEDAL_RELEASE = "40:40:30:40:0"
PEDAL_SETTLE_S = 0.6  # after pressing the pedal, before the strike: its thump has died away
PEDAL_LIFT_S = 0.3
PEDAL_RAW_FILE = common.PROJECT_DIR / "calibration" / "pedal_tests.json"

RAW_FILE = common.PROJECT_DIR / "calibration" / "hold_tests.json"
REPORT_FILE = common.PROJECT_DIR / "calibration" / "hold_tests.html"

# ============================================================================

TODAYS_DRIVE = "50 + hold"  # the variant that plays like the controller does
VARIANTS = {
    "long": lambda key, duty: f"P:{key}:{common.format_duty(duty)}:{LONG_STRIKE_MS}:0:0:0",
    "50 + hold": lambda key, duty: f"P:{key}:{common.format_duty(duty)}:{STRIKE_MS}:0:0:{HOLD_DUTY}",
    "50 only": lambda key, duty: f"P:{key}:{common.format_duty(duty)}:{STRIKE_MS}:0:0:0",
}
def duties_for(key: int, table: dict) -> list[float]:
    entry = table["keys"].get(str(key))
    if entry is None:
        sys.exit(f"key {key} isn't in {common.TABLE_FILE}")
    duty = entry["duty"]
    return sorted({duty[min(v, len(duty) - 1)] for v in VELOCITIES})


def load_raw(path: Path) -> list[dict]:
    if not path.exists():
        return []
    return json.loads(path.read_text())["flight"]


def save_key(path: Path, key: int, rows: list[dict]) -> None:
    """Writes one key's strikes, replacing any earlier ones at the same duties
    and variants, in one step so an interrupted run never loses data."""
    measured = {(r["duty"], r["variant"]) for r in rows}
    kept = [r for r in load_raw(path) if r["key"] != key or (r["duty"], r["variant"]) not in measured]
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(".tmp")
    lines = ",\n".join(json.dumps(r) for r in kept + rows)
    temp.write_text('{"flight": [\n' + lines + "\n]}\n")
    temp.replace(path)


def flight_key(key: int, duties: list[float], variants: list[str], ctx) -> list[dict]:
    rows: list[dict] = []
    rate = ctx.args.sample_rate
    print(f"\n== key {key} ({common.note_name(key)}): duties {', '.join(common.format_duty(d) for d in duties)} ==")

    def strike(duty: float, variant: str) -> dict:
        press = VARIANTS[variant](key, duty)
        while True:
            waited, quiet = common.wait_until_quiet(ctx.recorder, ctx.sos, ctx.room_db, common.QUIET_DB,
                                                    common.MAX_QUIET_WAIT_S)
            if not quiet:
                print(f"  still not quiet after {waited:.0f} s, striking anyway")
            recorded = common.record_press(ctx.timed, ctx.piano, press, f"R:{key}:0", PRESS_MS)
            if recorded is None:
                print("  network hiccup, doing it again")
                continue
            audio, send_s = recorded
            result, peak, noise = common.measure_strike(audio, rate, ctx.sos, ctx.args.margin_db)
            if quiet and noise > ctx.room_db + common.QUIET_DB + 3:
                print(f"  background too loud ({noise:.1f} dB), doing it again")
                continue
            break
        weighted = signal.sosfilt(ctx.sos, audio)
        at = int(rate * common.PRE_ROLL_MS / 1000)  # the press
        levels = common.levels_1ms(weighted[at:], rate)
        lands_ms = common.lands_at(levels, noise, ctx.args.margin_db)
        before = common.levels_1ms(weighted[at + ENVELOPE_MS[0] * rate // 1000:at], rate)
        envelope = np.concatenate([before, levels[: ENVELOPE_MS[1] // common.LANDING_STEP_MS]])
        hit = result == "hit"
        row = {"key": key, "duty": duty, "variant": variant, "result": result, "peak_db": round(peak, 2),
               "noise_db": round(noise, 2), "gain_db": ctx.args.gain_db, "command": press,
               "lands_ms": lands_ms if hit else None, "send_ms": round(send_s * 1000, 1),
               "margin_db": ctx.args.margin_db, "envelope_start_ms": ENVELOPE_MS[0],
               "envelope": [round(float(v), 1) for v in envelope]}
        lands = f"lands {lands_ms:4d} ms" if hit and lands_ms is not None else ""
        print(f"  {common.format_duty(duty):>6s}%  {variant:9s}  {result:9s} {peak:6.1f} dB  {lands:15s} {press}")
        return row

    # Interleave the variants, so slow drift (temperature, the room) affects them alike.
    for duty in duties:
        for _ in range(STRIKES):
            for variant in variants:
                rows.append(strike(duty, variant))
    return rows


class Context:
    def __init__(self, args):
        self.args = args
        self.piano = common.connect(args)
        self.recorder = common.Recorder(args.device, args.sample_rate)  # for waiting until quiet
        self.timed = common.TimedRecorder(args.device, args.sample_rate)  # for the strikes
        self.sos = common.a_weighting(args.sample_rate)
        self.room_db = common.measure_room(self.recorder, self.sos)


def flight(args) -> None:
    keys = common.parse_keys(args.keys)
    done = {(r["key"], r["duty"], r["variant"]) for r in load_raw(args.raw)}
    table = json.loads(common.TABLE_FILE.read_text()) if args.duties is None else None
    plan = {}
    for key in keys:
        duties = args.duties if args.duties is not None else duties_for(key, table)
        todo = [d for d in duties if args.redo or any((key, d, v) not in done for v in args.variants)]
        if todo:
            plan[key] = todo
    if not plan:
        print(f"Everything asked for is already in {args.raw} (use --redo to measure it again)")
        return
    strikes = sum(len(d) for d in plan.values()) * STRIKES * len(args.variants)
    print(f"{strikes} strikes on {len(plan)} key(s); roughly {strikes * 2.5 / 60:.0f} minutes")
    ctx = Context(args)
    try:
        for key, duties in plan.items():
            save_key(args.raw, key, flight_key(key, duties, args.variants, ctx))
    finally:
        ctx.piano.release_everything()


# --- report ------------------------------------------------------------------


def summarize(rows: list[dict]) -> list[dict]:
    """Per key, duty and variant: how often it played, median loudness and landing time."""
    grouped = defaultdict(list)
    for r in rows:
        grouped[(r["key"], r["duty"], r["variant"])].append(r)
    out = []
    for (key, duty, variant), group in sorted(grouped.items(), key=lambda kv: (kv[0][0], kv[0][1])):
        hits = [r for r in group if r["result"] == "hit"]
        lands = [r["lands_ms"] for r in hits if r["lands_ms"] is not None]
        out.append({"key": key, "duty": duty, "variant": variant, "strikes": len(group), "hits": len(hits),
                    "peak_db": statistics.median(r["peak_db"] - r["gain_db"] for r in hits) if hits else None,
                    "lands_ms": statistics.median(lands) if lands else None,
                    "lands_spread_ms": max(lands) - min(lands) if len(lands) > 1 else 0})
    return out


def redetect(rows: list[dict]) -> int:
    """Finds each hit's landing again from its saved levels, so results measured
    with an earlier version of the detector are judged the same way. Returns how
    many changed."""
    changed = 0
    for r in rows:
        if r["result"] != "hit":
            continue
        after = np.array(r["envelope"][-r.get("envelope_start_ms", -20) // common.LANDING_STEP_MS:])
        lands = common.lands_at(after, r["noise_db"], r.get("margin_db", common.MARGIN_DB))
        changed += lands != r["lands_ms"]
        r["lands_ms"] = lands
    return changed


def report(args) -> None:
    rows = load_raw(args.raw)
    if not rows:
        sys.exit(f"no measurements in {args.raw}: run `flight` first")
    changed = redetect(rows)
    if changed:
        print(f"{changed} landing time(s) differ from when they were measured (the detector has changed since)")
    summary = summarize(rows)
    by = {(s["key"], s["duty"], s["variant"]): s for s in summary}
    sends = [r["send_ms"] for r in rows]
    print(f"Sending a press took {statistics.median(sends):.1f} ms (median), {max(sends):.1f} ms at most; "
          "landing times are from when sending started.")

    def db(s):
        if s is None:
            return "-"
        if s["hits"] == 0:
            return "    silent"
        text = f"{s['peak_db']:6.1f} dB"
        return text + (f" {s['hits']}/{s['strikes']}" if s["hits"] < s["strikes"] else "    ")

    for key in sorted({s["key"] for s in summary}):
        print(f"\nkey {key} ({common.note_name(key)})")
        print(f"  {'duty':>6s}  {'lands':>12s}  {'long':>14s} {'50 + hold':>14s} {'50 only':>14s}"
              f"  {'hold adds':>9s}")
        for duty in sorted({s["duty"] for s in summary if s["key"] == key}):
            long, held, short = (by.get((key, duty, v)) for v in VARIANTS)
            lands = (f"{long['lands_ms']:5.0f} ±{long['lands_spread_ms'] / 2:<3.0f}ms"
                     if long and long["lands_ms"] is not None else f"{'-':>12s}")
            adds = ""
            if held and short and held["hits"] and short["hits"]:
                adds = f"{held['peak_db'] - short['peak_db']:+6.1f} dB"
            elif held and held["hits"] and short and not short["hits"]:
                adds = "  needed"
            print(f"  {common.format_duty(duty):>5s}%  {lands}  {db(long):>14s} {db(held):>14s} {db(short):>14s}"
                  f"  {adds:>9s}")
    print(f"\nlands: when the hammer hits the string on the long strike (half the spread across strikes). "
          f"hold adds: how much louder the note is with the {HOLD_DUTY}% hold after a {STRIKE_MS} ms strike "
          f"than without it (\"needed\": it only played with the hold).")

    template = (Path(__file__).resolve().parent / "hold_tests.html").read_text()
    data = {"summary": summary, "generated": time.strftime("%Y-%m-%d %H:%M"), "source": str(args.raw),
            "strike_ms": STRIKE_MS, "hold_duty": HOLD_DUTY, "long_ms": LONG_STRIKE_MS,
            "envelope_start_ms": ENVELOPE_MS[0], "variants": list(VARIANTS),
            "envelopes": [{"key": r["key"], "duty": r["duty"], "lands_ms": r["lands_ms"], "levels": r["envelope"]}
                          for r in rows if r["variant"] == "long" and r["result"] == "hit"]}
    page = template.replace("__HOLD_DATA__", json.dumps(data).replace("</", "<\\/"))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(page)
    print(f"\nwrote {args.out}")
    if args.open:
        import webbrowser

        webbrowser.open(args.out.resolve().as_uri())


# --- pedal -------------------------------------------------------------------


def table_command(key: int, velocity: int, table: dict) -> str:
    """The press the controller sends for this key and velocity."""
    entry = table["keys"][str(key)]
    landing = entry.get("landing_ms") or table.get("landing_ms")
    drive = {**common.DEFAULT_DRIVE, **table.get("drive", {})}
    duty = entry["duty"][velocity - 1]
    return common.drive_command(key, duty, landing[velocity - 1] if landing else None, drive, entry)


def set_pedal(piano, down: bool) -> None:
    for channel in PEDAL_CHANNELS:
        piano.send(f"P:{channel}:{PEDAL_PRESS if down else PEDAL_RELEASE}")
    time.sleep(PEDAL_SETTLE_S if down else PEDAL_LIFT_S)


def pedal_key(key: int, table: dict, ctx) -> list[dict]:
    rows: list[dict] = []
    rate = ctx.args.sample_rate
    duties = table["keys"][str(key)]["duty"]
    print(f"\n== key {key} ({common.note_name(key)}) ==")

    def strike(velocity: int, way: str, pedal: bool) -> dict:
        duty = duties[velocity - 1]
        press = VARIANTS["long"](key, duty) if way == "long" else table_command(key, velocity, table)
        while True:
            waited, quiet = common.wait_until_quiet(ctx.recorder, ctx.sos, ctx.room_db, common.QUIET_DB,
                                                    common.MAX_QUIET_WAIT_S)
            if pedal:
                set_pedal(ctx.piano, True)
            try:
                recorded = common.record_press(ctx.timed, ctx.piano, press, f"R:{key}:0", PRESS_MS)
            finally:
                if pedal:
                    set_pedal(ctx.piano, False)
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
    for velocity in PEDAL_VELOCITIES:
        for _ in range(PEDAL_STRIKES):
            for way in ("long", "table"):
                for pedal in (False, True):
                    rows.append(strike(velocity, way, pedal))
    return rows


def pedal(args) -> None:
    table = json.loads(common.TABLE_FILE.read_text())
    done = {r["key"] for r in load_pedal(args.raw)}
    keys = [k for k in common.parse_keys(args.keys) if args.redo or k not in done]
    missing = [k for k in keys if str(k) not in table["keys"]]
    if missing:
        sys.exit(f"keys {missing} aren't in {common.TABLE_FILE}")
    if not keys:
        print(f"Every key asked for is already in {args.raw} (use --redo to measure it again)")
        return
    strikes = len(keys) * len(PEDAL_VELOCITIES) * PEDAL_STRIKES * 4
    print(f"{strikes} strikes on {len(keys)} key(s); roughly {strikes * 3.5 / 60:.0f} minutes")
    ctx = Context(args)
    try:
        for key in keys:
            rows = pedal_key(key, table, ctx)
            kept = [r for r in load_pedal(args.raw) if r["key"] != key]
            args.raw.parent.mkdir(parents=True, exist_ok=True)
            temp = args.raw.with_suffix(".tmp")
            temp.write_text('{"pedal": [\n' + ",\n".join(json.dumps(r) for r in kept + rows) + "\n]}\n")
            temp.replace(args.raw)
    finally:
        ctx.piano.release_everything()


def load_pedal(path: Path) -> list[dict]:
    if not path.exists():
        return []
    return json.loads(path.read_text())["pedal"]


def pedal_report(args) -> None:
    rows = load_pedal(args.raw)
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


# --- cli ---------------------------------------------------------------------


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)

    p = commands.add_parser("flight", help="time when the hammer lands, across each test key's duties")
    p.add_argument("--keys", default=KEYS)
    p.add_argument("--duties", type=lambda v: [float(d) for d in v.split(",")],
                   help="comma-separated duties, e.g. 18,20,22 (default: from the velocity table)")
    p.add_argument("--variants", nargs="+", choices=list(VARIANTS), default=list(VARIANTS))
    p.add_argument("--redo", action="store_true", help="measure again, replacing earlier results")
    p.add_argument("--raw", type=Path, default=RAW_FILE)
    p.add_argument("--connection", choices=["usb", "wifi"], default=common.CONNECTION)
    p.add_argument("--port", default=common.SERIAL_PORT)
    p.add_argument("--host", default=common.HOST)
    p.add_argument("--device", default=common.DEVICE, type=lambda v: int(v) if v.isdigit() else v)
    p.add_argument("--sample-rate", type=int, default=common.SAMPLE_RATE)
    p.add_argument("--margin-db", type=float, default=common.MARGIN_DB)
    p.add_argument("--gain-db", type=float, default=common.MIC_GAIN_DB)
    p.set_defaults(run=flight)

    p = commands.add_parser("pedal", help="strike test keys with the sustain pedal up and down")
    p.add_argument("--keys", default=PEDAL_KEYS)
    p.add_argument("--redo", action="store_true", help="measure again, replacing earlier results")
    p.add_argument("--raw", type=Path, default=PEDAL_RAW_FILE)
    p.add_argument("--connection", choices=["usb", "wifi"], default=common.CONNECTION)
    p.add_argument("--port", default=common.SERIAL_PORT)
    p.add_argument("--host", default=common.HOST)
    p.add_argument("--device", default=common.DEVICE, type=lambda v: int(v) if v.isdigit() else v)
    p.add_argument("--sample-rate", type=int, default=common.SAMPLE_RATE)
    p.add_argument("--margin-db", type=float, default=common.MARGIN_DB)
    p.add_argument("--gain-db", type=float, default=common.MIC_GAIN_DB)
    p.set_defaults(run=pedal)

    p = commands.add_parser("pedal-report", help="print the pedal comparison")
    p.add_argument("--raw", type=Path, default=PEDAL_RAW_FILE)
    p.set_defaults(run=pedal_report)

    p = commands.add_parser("report", help="print the results and write calibration/hold_tests.html")
    p.add_argument("--raw", type=Path, default=RAW_FILE)
    p.add_argument("--out", type=Path, default=REPORT_FILE)
    p.add_argument("--no-open", dest="open", action="store_false")
    p.set_defaults(run=report)

    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
