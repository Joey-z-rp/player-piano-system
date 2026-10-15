#!/usr/bin/env python3
"""Compare ways of driving the solenoids, measured with the microphone.

Each drive family turns one control value into a driver command: the strike
duty for "duty" (today's drive), the pulse duty with a trough and hold that
follow it for "trough", the strike length at
full power for "pulse", the strike length at PULSE_LOW_DUTY for
"pulse_low", and the strike duty for "long", whose strike lasts until the
hammer has landed. For a few test keys, each family's control value is
swept from where the key starts to sound to where it stops getting louder,
recording each strike's loudness, whether the hammer struck twice (bounce),
and how fast the note died away while held (damping).

  measure   sweep each family on each test key (saves as it goes; rerun to resume)
  compare   print a scorecard per family and write calibration/drives.html

The connection, microphone and strike settings are in calibration_common.py;
the families and the sweep are in the SETTINGS block below.
"""

from __future__ import annotations

import argparse
import csv
import json
import statistics
import sys
import time
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import numpy as np
from scipy import signal

sys.path.insert(0, str(Path(__file__).resolve().parent))
import calibration_common as common  # noqa: E402

# === SETTINGS ===============================================================

KEYS = "10,40,70"  # test keys: low, middle and high
FAMILIES_TO_RUN = ["duty", "trough", "pulse", "pulse_low", "long"]

HOLD_DUTY = 20  # % held after the strike by the duty and pulse families
TROUGH_MAX_HOLD = 60  # % the trough family's hold (which follows its trough) never exceeds
PULSE_LOW_DUTY = 60  # % the pulse_low family pushes at, for a variable time
LONG_STRIKE_MS = 250  # the long family's strike: long enough for the hammer to land at any duty
STRIKES = 3  # strikes per level; 2 more when they disagree by over SPREAD_DB
SPREAD_DB = 1.5
FLAT_DB = 0.5  # two search-step spans in a row adding less than this = saturated

# Bounce and decay are measured as set in calibration_common.py.

RAW_FILE = common.PROJECT_DIR / "calibration" / "drives.csv"
REPORT_FILE = common.PROJECT_DIR / "calibration" / "drives.html"


def duty_drive(key: int, duty: float) -> str:
    """Today's drive: the strike duty for 50 ms, then the hold."""
    return f"P:{key}:{common.format_duty(duty)}:50:0:0:{HOLD_DUTY}"


def trough_drive(key: int, duty: float) -> str:
    """A 50 ms pulse, then a trough 12% lower that
    carries the hammer on without speeding it up (longer for soft notes, 5 ms
    for the loudest), then a hold equal to the trough, up to TROUGH_MAX_HOLD."""
    trough = max(duty - 12, 0)
    trough_ms = max(5, round(105 - duty))
    hold = min(trough, TROUGH_MAX_HOLD)
    return (f"P:{key}:{common.format_duty(duty)}:50:{common.format_duty(trough)}:{trough_ms}:"
            f"{common.format_duty(hold)}")


def pulse_drive(key: int, ms: float) -> str:
    """Full power for a variable time, then the hold."""
    return f"P:{key}:100:{round(ms)}:0:0:{HOLD_DUTY}"


def pulse_low_drive(key: int, ms: float) -> str:
    """PULSE_LOW_DUTY for a variable time, then the hold: like pulse, but
    gentler, to reach softer notes."""
    return f"P:{key}:{common.format_duty(PULSE_LOW_DUTY)}:{round(ms)}:0:0:{HOLD_DUTY}"


def long_drive(key: int, duty: float) -> str:
    """The strike lasts LONG_STRIKE_MS, with no hold, so the hold never helps push
    the hammer: a duty that hasn't landed by then doesn't count as playing."""
    return f"P:{key}:{common.format_duty(duty)}:{LONG_STRIKE_MS}:0:0:0"


@dataclass(frozen=True)
class Family:
    name: str
    unit: str
    start: float  # first control value tried
    search_step: float  # step while looking for the first sound, and above the fine region
    fine_step: float  # step around the threshold
    fine_range: float  # how far above the first sound to keep using fine_step
    max: float
    command: Callable[[int, float], str]
    press_ms: int | None = None  # how long the key is held before the release; None = --hold-ms


FAMILIES = {
    "duty": Family("duty", "%", 20, 5, 0.5, 12, 100, duty_drive),
    "trough": Family("trough", "%", 20, 5, 0.5, 12, 100, trough_drive),
    "pulse": Family("pulse", "ms", 2, 4, 1, 16, 80, pulse_drive),
    "pulse_low": Family("pulse_low", "ms", 4, 6, 1, 24, 150, pulse_low_drive),
    # Held longer than the others, so a note that lands late isn't cut short by the release.
    "long": Family("long", "%", 10, 5, 0.5, 12, 85, long_drive, press_ms=LONG_STRIKE_MS + 250),
}

# ============================================================================

RAW_FIELDS = ["family", "key", "value", "repeat", "hit", "peak_db", "noise_db",
              "sample_peak_db", "gain_db", "bounce", "decay_db"]


# --- measuring ---------------------------------------------------------------


def load_raw(path: Path) -> list[dict]:
    if not path.exists():
        return []
    with path.open() as f:
        return [{"family": r["family"], "key": int(r["key"]), "value": float(r["value"]),
                 "repeat": int(r["repeat"]), "hit": r["hit"] == "1", "peak_db": float(r["peak_db"]),
                 "noise_db": float(r["noise_db"]), "sample_peak_db": float(r["sample_peak_db"]),
                 "gain_db": float(r["gain_db"]), "bounce": r["bounce"] == "1", "decay_db": float(r["decay_db"])}
                for r in csv.DictReader(f)]


def save_sweep(path: Path, family: str, key: int, rows: list[dict]) -> None:
    """Writes one family's sweep of one key, replacing any earlier one. The
    file is replaced in one step, so an interrupted run never loses data."""
    kept = [r for r in load_raw(path) if (r["family"], r["key"]) != (family, key)]
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(".tmp")
    with temp.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=RAW_FIELDS)
        writer.writeheader()
        for r in kept + rows:
            writer.writerow({**r, "hit": int(r["hit"]), "bounce": int(r["bounce"]),
                             **{k: f"{r[k]:.2f}" for k in ("peak_db", "noise_db", "sample_peak_db", "decay_db")}})
    temp.replace(path)


def sweep(family: Family, key: int, ctx) -> list[dict]:
    """Looks for where the key starts to sound, one strike per search step,
    then measures each level from just below there: fine steps over the
    fine range, search steps after that, until the loudness stops rising."""
    rows: list[dict] = []
    press_ms = family.press_ms or ctx.args.hold_ms
    seconds = (common.PRE_ROLL_MS + press_ms + common.TAIL_MS) / 1000
    print(f"\n== {family.name}: key {key} ({common.note_name(key)}) ==")

    def strike(value: float, label: str = "strike") -> dict:
        while True:
            waited, quiet = common.wait_until_quiet(ctx.recorder, ctx.sos, ctx.room_db, common.QUIET_DB,
                                                    common.MAX_QUIET_WAIT_S)
            if not quiet:
                print(f"  {value:6g}{family.unit}  still not quiet after {waited:.0f} s, striking anyway")
            clean = []
            press = family.command(key, value)
            audio = ctx.recorder.record(seconds, lambda: clean.append(
                ctx.piano.play(press, f"R:{key}:0", press_ms)))
            if not clean[0]:
                print(f"  {value:6g}{family.unit}  network hiccup, doing it again")
                continue
            result, peak, noise = common.measure_strike(audio, ctx.args.sample_rate, ctx.sos, ctx.args.margin_db)
            if quiet and noise > ctx.room_db + common.QUIET_DB + 3:
                print(f"  {value:6g}{family.unit}  background too loud ({noise:.1f} dB), doing it again")
                continue
            break
        bounce, decay = common.bounce_and_decay(audio, ctx.args.sample_rate, ctx.sos, noise, ctx.args.margin_db)
        hit = result == "hit"
        row = {"family": family.name, "key": key, "value": value, "repeat": len(rows), "hit": hit,
               "peak_db": peak, "noise_db": noise,
               "sample_peak_db": 20 * np.log10(max(float(np.abs(audio).max()), 1e-10)),
               "gain_db": ctx.args.gain_db, "bounce": hit and bounce, "decay_db": decay if hit else float("nan")}
        rows.append(row)
        flags = ("  BOUNCE" if row["bounce"] else "") + ("  CLIPPED" if row["sample_peak_db"] > common.CLIP_DBFS else "")
        print(f"  {value:6g}{family.unit}  {label:6s} {result:9s} {peak:6.1f} dB  {press}{flags}")
        return row

    first_sound = None
    value = family.start
    while value <= family.max + 1e-9:
        if strike(value, "search")["hit"]:
            first_sound = value
            break
        value = round(value + family.search_step, 2)
    if first_sound is None:
        print("  never sounded")
        return rows

    values, value = [], max(family.start, round(first_sound - family.search_step + family.fine_step, 2))
    while value <= min(first_sound + family.fine_range, family.max) + 1e-9:
        values.append(value)
        value = round(value + family.fine_step, 2)
    value = round(values[-1] + family.search_step, 2)
    while value <= family.max + 1e-9:
        values.append(value)
        value = round(value + family.search_step, 2)

    curve: list[tuple[float, float]] = []
    for value in values:
        level = [strike(value) for _ in range(STRIKES)]
        peaks = [r["peak_db"] for r in level if r["hit"]]
        if len(peaks) == len(level) and max(peaks) - min(peaks) > SPREAD_DB:
            level += [strike(value) for _ in range(2)]
            peaks = [r["peak_db"] for r in level if r["hit"]]
        if len(peaks) == len(level):
            curve.append((value, statistics.median(peaks)))
            if common.saturation_start(curve, family.search_step, FLAT_DB) is not None:
                print(f"  stopped getting louder, moving on")
                break
    return rows


class Context:
    def __init__(self, args):
        self.args = args
        self.piano = common.connect(args)
        self.recorder = common.Recorder(args.device, args.sample_rate)
        self.sos = common.a_weighting(args.sample_rate)
        self.room_db = common.measure_room(self.recorder, self.sos)


def measure(args) -> None:
    keys = common.parse_keys(args.keys)
    families = [FAMILIES[name] for name in args.families]
    done = {(r["family"], r["key"]) for r in load_raw(args.raw)}
    todo = [(f, k) for f in families for k in keys if args.redo or (f.name, k) not in done]
    if not todo:
        print(f"Everything asked for is already in {args.raw} (use --redo to measure it again)")
        return
    print(f"{len(todo)} sweeps; roughly 3-5 minutes each")
    ctx = Context(args)
    try:
        # One key at a time through every family, so a key's sweeps sit close together in time.
        for key in keys:
            for family in families:
                if (family, key) in todo:
                    save_sweep(args.raw, family.name, key, sweep(family, key, ctx))
    finally:
        ctx.piano.release_everything()


# --- comparing ---------------------------------------------------------------


def summarize(family: Family, rows: list[dict]) -> dict | None:
    """One family's sweep of one key: where it plays reliably, how loud, how
    evenly the loudness rises, and how often the hammer bounced."""
    by_value = defaultdict(list)
    for r in rows:
        by_value[r["value"]].append(r)
    values = sorted(by_value)
    reliable = {v for v in values if all(r["hit"] for r in by_value[v])}
    threshold = None
    for v in reversed(values):
        if v not in reliable:
            break
        threshold = v
    if threshold is None:
        return None

    curve, loudest = [], -np.inf
    for v in values:
        if v >= threshold:
            loudest = max(loudest, statistics.median(r["peak_db"] - r["gain_db"] for r in by_value[v]))
            curve.append((v, loudest))
    top = common.saturation_start(curve, family.search_step, FLAT_DB) or curve[-1][0]
    usable = [(v, db) for v, db in curve if v <= top]
    soft, loud = usable[0][1], usable[-1][1]
    span = top - threshold
    level_at = lambda v: float(np.interp(v, [p[0] for p in usable], [p[1] for p in usable]))  # noqa: E731
    first_tenth = (level_at(threshold + 0.1 * span) - soft) / (loud - soft) if loud > soft and span > 0 else 1.0
    upper = [r for v in values if threshold + span / 2 <= v <= top for r in by_value[v] if r["hit"]]
    spreads = [max(p) - min(p) for v in reliable for p in [[r["peak_db"] for r in by_value[v]]] if len(p) > 1]
    decays = [r["decay_db"] for v in values if threshold <= v <= top for r in by_value[v] if r["hit"]]
    return {
        "threshold": threshold, "max": top, "soft_db": soft, "loud_db": loud, "range_db": loud - soft,
        "first_tenth": first_tenth,
        "spread_db": statistics.median(spreads) if spreads else 0.0,
        "bounce_rate": sum(r["bounce"] for r in upper) / len(upper) if upper else 0.0,
        "decay_db": statistics.median(decays) if decays else float("nan"),
        "curve": [[round((v - threshold) / span, 4) if span else 0, round(db, 2), v] for v, db in usable],
        "strikes": len(rows),
    }


def compare(args) -> None:
    rows = load_raw(args.raw)
    if not rows:
        sys.exit(f"no measurements in {args.raw}: run `measure` first")
    grouped = defaultdict(list)
    for r in rows:
        grouped[(r["family"], r["key"])].append(r)

    results = []
    print(f"{'family':10s} {'key':>8s}  {'plays from':>10s} {'up to':>8s} {'range':>8s} {'softest':>8s} "
          f"{'1st tenth':>9s} {'spread':>7s} {'bounce':>7s} {'decay':>7s}")
    for name, family in FAMILIES.items():
        for key in sorted({k for f, k in grouped if f == name}):
            s = summarize(family, grouped[(name, key)])
            label = f"{key} {common.note_name(key)}"
            if s is None:
                print(f"{name:10s} {label:>8s}  never played reliably")
                continue
            results.append({"family": name, "key": key, "note": common.note_name(key), "unit": family.unit, **s})
            print(f"{name:10s} {label:>8s}  {s['threshold']:>8g}{family.unit:<2s} {s['max']:>6g}{family.unit:<2s} "
                  f"{s['range_db']:6.1f}dB {s['soft_db']:6.1f}dB {s['first_tenth']:8.0%} {s['spread_db']:5.2f}dB "
                  f"{s['bounce_rate']:6.0%} {s['decay_db']:5.1f}dB")

    print("\nper family (median across keys):")
    for name in FAMILIES:
        mine = [r for r in results if r["family"] == name]
        if mine:
            med = lambda f: statistics.median(r[f] for r in mine)  # noqa: E731
            print(f"  {name:10s} range {med('range_db'):5.1f} dB, first tenth {med('first_tenth'):4.0%}, "
                  f"spread {med('spread_db'):.2f} dB, bounce {med('bounce_rate'):4.0%}, decay {med('decay_db'):.1f} dB")
    print("\nrange: softest reliable to loudest. first tenth: share of the range in the first tenth of the "
          "control's usable span (10% = perfectly even; higher = bunched at the soft end). spread: strike-to-strike "
          "difference at one setting. bounce: strikes in the upper half that struck twice. decay: dB lost "
          f"{common.DECAY_AT_MS} ms after the attack while held (higher = damped sooner).")

    template = (Path(__file__).resolve().parent / "drive_comparison.html").read_text()
    data = {"results": results, "generated": time.strftime("%Y-%m-%d %H:%M"), "source": str(args.raw),
            "decay_ms": common.DECAY_AT_MS}
    page = template.replace("__DRIVE_DATA__", json.dumps(data).replace("</", "<\\/"))
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

    p = commands.add_parser("measure", help="sweep each drive family on the test keys")
    p.add_argument("--keys", default=KEYS)
    p.add_argument("--families", nargs="+", choices=list(FAMILIES), default=FAMILIES_TO_RUN)
    p.add_argument("--redo", action="store_true", help="measure again, replacing earlier sweeps")
    p.add_argument("--raw", type=Path, default=RAW_FILE)
    p.add_argument("--connection", choices=["usb", "wifi"], default=common.CONNECTION)
    p.add_argument("--port", default=common.SERIAL_PORT)
    p.add_argument("--host", default=common.HOST)
    p.add_argument("--device", default=common.DEVICE, type=lambda v: int(v) if v.isdigit() else v)
    p.add_argument("--sample-rate", type=int, default=common.SAMPLE_RATE)
    p.add_argument("--hold-ms", type=int, default=common.HOLD_MS)
    p.add_argument("--margin-db", type=float, default=common.MARGIN_DB)
    p.add_argument("--gain-db", type=float, default=common.MIC_GAIN_DB)
    p.set_defaults(run=measure)

    p = commands.add_parser("compare", help="scorecard per family, and calibration/drives.html")
    p.add_argument("--raw", type=Path, default=RAW_FILE)
    p.add_argument("--out", type=Path, default=REPORT_FILE)
    p.add_argument("--no-open", dest="open", action="store_false")
    p.set_defaults(run=compare)

    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
