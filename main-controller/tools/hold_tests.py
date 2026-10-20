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
    ctx = common.Session(args)
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
    common.add_session_args(p)
    p.set_defaults(run=flight)

    p = commands.add_parser("report", help="print the results and write calibration/hold_tests.html")
    p.add_argument("--raw", type=Path, default=RAW_FILE)
    p.add_argument("--out", type=Path, default=REPORT_FILE)
    p.add_argument("--no-open", dest="open", action="store_false")
    p.set_defaults(run=report)

    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
