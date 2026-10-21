"""Analysis: each key's summary (where it starts to play, its loudness and
landing curves, flags), the velocity mapping, and the table and report
commands. The settings come from calibrate_dynamics.py's SETTINGS block, as
args.cfg."""

from __future__ import annotations

import json
import statistics
import time
from collections import defaultdict
from pathlib import Path

import numpy as np

import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from calibration_common import (  # noqa: E402
    CLIP_DBFS,
    DECAY_AT_MS,
    KEY_DRIVE_FIELDS,
    PIANO_KEYS,
    format_duty,
    note_name,
    saturation_start,
)
from calibration_data import drive_settings, key_drive, load_by_key, load_flags  # noqa: E402


OFFSETS_NOTE = ("If keys have offsets on the controller (the test page marks them), they were set by ear against "
                "the old table: listen again and adjust or clear them.")

def is_cut_short(rows: list[dict], cut_short_db: float) -> bool:
    """One miss among at least 3 hits, as loud as the hits: the string was
    struck, but the note stopped early."""
    hits = [r["peak_db"] for r in rows if r["hit"]]
    misses = [r["peak_db"] for r in rows if not r["hit"]]
    return (
        len(misses) == 1
        and len(hits) >= 3
        and misses[0] >= statistics.median(hits) - cut_short_db
    )


def is_one_failed(levels: dict, duties: list, i: int) -> bool:
    """One strike that failed among at least 2 hits, with the levels either
    side reliable: a glitch, not where the key stops playing (pedal-down only)."""
    rows = levels[duties[i]]
    misses = sum(not r["hit"] for r in rows)
    either_side = [duties[j] for j in (i - 1, i + 1) if 0 <= j < len(duties)]
    return (misses == 1 and len(rows) - misses >= 2 and len(either_side) == 2
            and all(all(r["hit"] for r in levels[d]) for d in either_side))


def summarize_key(levels: dict[int, list[dict]], args, pedal: bool = False) -> dict | None:
    """Applies the analysis rules to one key's strikes, grouped by duty.
    Returns None if the key never played reliably. pedal: measured with the
    pedal down, where a single failed strike between reliable levels is also
    tolerated (see is_one_failed)."""
    duties = sorted(levels)
    reliable = [d for d in duties if all(r["hit"] for r in levels[d])]
    # min: lowest reliable duty with every level above it also reliable,
    # apart from single cut-short strikes.
    threshold = None
    cut_short = []
    for duty in reversed(duties):
        if len(levels[duty]) < args.cfg.MIN_LEVEL_STRIKES:
            continue  # a single search strike says little about reliability
        if duty in reliable:
            threshold = duty
        elif threshold is not None and (is_cut_short(levels[duty], args.cut_short_db) or
                                        (pedal and is_one_failed(levels, duties, duties.index(duty)))):
            cut_short.append(duty)
        else:
            break
    if threshold is None:
        return None

    curve = []
    loudest = -np.inf
    for duty in duties:
        if duty < threshold:
            continue
        # In the first session's terms, so keys measured at different gains compare.
        median = statistics.median(
            r["peak_db"] - r["gain_db"] for r in levels[duty] if r["hit"]
        )
        loudest = max(loudest, median)  # keep the curve rising so it can be inverted
        curve.append([duty, round(loudest, 2)])

    # max: where two DUTY_STEP spans in a row add under --flat-db, or with
    # MAX_AT_LOUDEST the loudest duty measured (the first to reach the top level).
    max_duty = saturation_start(curve, args.step, args.flat_db) or curve[-1][0]
    if args.cfg.MAX_AT_LOUDEST:
        max_duty = next(duty for duty, db in curve if db == curve[-1][1])
    min_duty = min(threshold + args.min_margin, max_duty)
    played = [r for d in duties if threshold <= d <= max_duty for r in levels[d] if r["hit"]]
    landing = []
    for duty in duties:
        lands = [r["lands_ms"] for r in levels[duty] if r["hit"] and r["lands_ms"] is not None]
        if duty >= threshold and lands:
            landing.append([duty, statistics.median(lands)])
    return {
        "key": next(iter(levels.values()))[0]["key"],
        "threshold": threshold,
        "lowest_measured": duties[0],
        # [duty, ms from the press until the hammer lands], where measured.
        "landing": landing,
        # Duties where a strike bounced, for listening to.
        "bounced": sorted({r["duty"] for r in played if r["bounce"]}),
        "decay_db": statistics.median(r["decay_db"] for r in played) if played else float("nan"),
        "min": min_duty,
        # The softest the key will be asked to play: its level at the min duty.
        "min_db": round(
            float(np.interp(min_duty, [d for d, _ in curve], [db for _, db in curve])),
            2,
        ),
        "max": max_duty,
        "curve": curve,
        "soft_db": curve[0][1],  # softest measured, at the threshold
        "loud_db": max(db for duty, db in curve if duty <= max_duty),
        "cut_short": sorted(d for d in cut_short if d > threshold),
        "clipped": sum(
            1 for d in duties for r in levels[d] if r["sample_peak_db"] > CLIP_DBFS
        ),
    }


def duty_for_level(summary: dict, target_db: float, resolution: float) -> float:
    """The duty that plays target_db on a key, read off its curve: clamped to
    the levels the key can reach and to its safe minimum and maximum duty, in
    DUTY_RESOLUTION steps."""
    levels, duties = [], []
    for duty, db in summary["curve"]:
        # The curve never falls but can be flat: keep the lowest duty for each level.
        if duty <= summary["max"] and (not levels or db > levels[-1]):
            levels.append(db)
            duties.append(duty)
    duty = float(np.interp(target_db, levels, duties))  # holds at the ends
    duty = min(max(duty, summary["min"]), summary["max"])
    return round(round(duty / resolution) * resolution, 2)


def level_at_duty(summary: dict, duty: float) -> float:
    curve = summary["curve"]
    return float(np.interp(duty, [d for d, _ in curve], [db for _, db in curve]))


def limit_landing(summary: dict, max_landing_ms: float, resolution: float) -> float | None:
    """Raises a key's min to the lowest duty whose hammer lands within
    max_landing_ms (landing falls as the duty rises). Returns the min it had,
    if it changed."""
    duties, lands = zip(*summary["landing"])
    if float(np.interp(summary["min"], duties, lands)) <= max_landing_ms:
        return None
    was = summary["min"]
    duty = was
    while duty < summary["max"] and float(np.interp(duty, duties, lands)) > max_landing_ms:
        duty = round(duty + resolution, 2)
    summary["min"] = duty
    summary["min_db"] = round(level_at_duty(summary, duty), 2)
    return was


def build_mapping(by_key: dict[int, dict[int, list[dict]]], args, flags: dict | None = None,
                  pedal: bool = False, max_landing_ms: float | None = None) -> dict[int, dict]:
    """Each key that plays reliably: its summary (see summarize_key), its
    neighbour-smoothed softest and loudest levels, and the duty for each
    velocity 1-127. The softest end is what a key plays at its min duty, not
    the softer level measured at its threshold, which it is never asked for.
    Each end moves SMOOTHING of the way from the key's own level towards the
    median of its neighbours'."""
    summaries = {}
    for key, levels in sorted(by_key.items()):
        summary = summarize_key(levels, args, pedal)
        if summary is not None:
            if max_landing_ms is not None and summary["landing"]:
                summary["min_raised_from"] = limit_landing(summary, max_landing_ms, args.cfg.DUTY_RESOLUTION)
            summaries[key] = summary

    mapping = {}
    for key, summary in summaries.items():
        near = [
            n for n in summaries.values() if abs(n["key"] - key) <= args.neighbour_keys
        ]
        soft = summary["min_db"] + args.smoothing * (statistics.median(n["min_db"] for n in near) - summary["min_db"])
        loud = summary["loud_db"] + args.smoothing * (statistics.median(n["loud_db"] for n in near) - summary["loud_db"])
        offset = args.tilt_db * (1 - key / (PIANO_KEYS - 1)) + args.cfg.KEY_TRIMS_DB.get(
            key, 0.0
        )
        duties = [
            duty_for_level(summary, soft + (v - 1) / 126 * (loud - soft) + offset, args.cfg.DUTY_RESOLUTION)
            for v in range(1, 128)
        ]
        decays = [n["decay_db"] for n in near if n["key"] != key and n["decay_db"] == n["decay_db"]]
        damped = bool(decays) and summary["decay_db"] > statistics.median(decays) + args.damped_db
        mapping[key] = {
            **summary,
            "damped": damped,
            "by_ear": {k: v for k, v in (flags or {}).get("keys", {}).get(str(key), {}).items() if v},
            "smooth_soft_db": soft,
            "smooth_loud_db": loud,
            "offset_db": offset,
            "duties": duties,
        }
    return mapping


def print_key_log(by_key: dict, mapping: dict) -> None:
    for key in sorted(by_key):
        m = mapping.get(key)
        if m is None:
            print(
                f"key {key:2d}: never played reliably, left out (it plays the default mapping)"
            )
            continue
        duties = lambda ds: ", ".join(f"{format_duty(d)}%" for d in ds)  # noqa: E731
        note = ""
        if m["cut_short"]:
            note = f"  (ignored a cut-short strike at {duties(m['cut_short'])})"
        if m["bounced"]:
            note += f"  bounced at {duties(m['bounced'])}"
        if m["damped"]:
            note += f"  damped (notes die away {m['decay_db']:.0f} dB in {DECAY_AT_MS} ms)"
        if m["by_ear"]:
            heard = [k for k in ("damped", "bounces") if m["by_ear"].get(k)]
            comment = m["by_ear"].get("comment")
            note += f"  BY EAR: {', '.join(heard)}{'; ' if heard and comment else ''}{comment or ''}"
        if m["clipped"]:
            note += f"  !! CLIPPED in {m['clipped']} strikes: redo at a lower mic gain"
        print(
            f"key {key:2d}: min {format_duty(m['min']):>5s}  max {format_duty(m['max']):>5s}  "
            f"range {m['loud_db'] - m['min_db']:5.1f} dB  ({m['min_db']:.1f} to {m['loud_db']:.1f}){note}"
        )
    lowest = [k for k in sorted(mapping) if mapping[k]["threshold"] == mapping[k]["lowest_measured"] and mapping[k]["landing"]]
    if lowest:
        print(f"\nPlay reliably at the lowest duty measured, so may start lower (fine with MIN_MARGIN; "
              f"--below-start measures lower): {', '.join(map(str, lowest))}")
    listen = [k for k in sorted(mapping) if mapping[k]["bounced"] or mapping[k]["damped"] or mapping[k]["by_ear"]]
    if listen:
        print(f"\nBounced somewhere in their range, or damped (worth a listen): {', '.join(map(str, listen))}")


def own_landing(m: dict) -> list[int] | None:
    """A key's landing time (ms) at each velocity, from its own calibration
    strikes, read at the duty it plays each velocity at. None if it doesn't
    have landing times at two or more duties."""
    if len(m["landing"]) < 2:
        return None
    duties, lands = zip(*m["landing"])
    return [int(np.ceil(v)) for v in np.interp(m["duties"], duties, lands)]


def flight_test_landing(mapping: dict, path: Path) -> tuple[list[int], list[int]] | None:
    """Median landing time (ms) over the keys `hold_tests.py flight` tested, at
    each velocity, and those keys. Each key's landing times are measured
    against duty, so they're read at the duty that key's table gives each
    velocity."""
    if not path.exists():
        return None
    import hold_tests

    rows = hold_tests.load_raw(path)
    hold_tests.redetect(rows)
    points = defaultdict(list)
    for s in hold_tests.summarize(rows):
        if s["variant"] == hold_tests.TODAYS_DRIVE and s["lands_ms"] is not None:
            points[s["key"]].append((s["duty"], s["lands_ms"]))
    keys = sorted(k for k in points if k in mapping and len(points[k]) >= 2)
    if not keys:
        return None
    per_key = []
    for key in keys:
        duties, lands = zip(*sorted(points[key]))
        per_key.append(np.interp(mapping[key]["duties"], duties, lands))
    return [int(np.ceil(v)) for v in np.median(per_key, axis=0)], keys


def pedal_entries(args, mapping: dict, up_latest_ms: int | None) -> dict[int, dict]:
    """Each key measured with the pedal down as well as up: its "pedal" entry.
    The pedal-down measurements are mapped onto velocities on their own,
    exactly as the pedal-up ones are (see build_mapping): velocity 1 is the
    key's softest reliable level with the pedal down, 127 its loudest, each
    smoothed with its neighbours'. So the two are never compared, and
    measuring them in different sessions doesn't matter. With the landing
    times at those duties, and the key's drive with the pedal down. A key's
    softest notes are left out where they'd land later than
    PEDAL_MAX_LANDING_MS ("up": up_latest_ms, the latest with the pedal up)."""
    if not args.pedal_raw.exists():
        return {}
    print(f"\nWith the pedal down ({args.pedal_raw.name}):")
    by_key = load_by_key(args.pedal_raw, args.pedal_recordings, args.margin_db, None, args.cfg.IGNORE_FIRST_STRIKE,
                         pedal=True)
    limit = args.cfg.PEDAL_MAX_LANDING_MS
    limit = up_latest_ms if limit == "up" else limit
    if limit is not None:
        print(f"Hammers land within {limit} ms (PEDAL_MAX_LANDING_MS)")
    down = build_mapping(by_key, args, None, pedal=True, max_landing_ms=limit)
    entries = {}
    for key in sorted(by_key):
        m = down.get(key)
        landing = own_landing(m) if m is not None else None
        if key not in mapping or m is None or landing is None:
            why = ("it isn't calibrated with the pedal up" if key not in mapping else
                   "it never played reliably" if m is None else "too few landing times")
            print(f"key {key:2d}: left out, {why} (it plays as with the pedal up)")
            continue
        entries[key] = {
            "duty": m["duties"],
            "landing_ms": landing,
            **key_drive(key, args.cfg, pedal=True),
            "min": m["min"],
            "max": m["max"],
            "min_db": m["min_db"],
            "loud_db": round(m["loud_db"], 2),
            "smooth_soft_db": round(m["smooth_soft_db"], 2),
            "smooth_loud_db": round(m["smooth_loud_db"], 2),
            "offset_db": round(m["offset_db"], 2),
            "curve": m["curve"],
        }
        up = mapping[key]
        up_landing = own_landing(up)
        lands = (f", lands {up_landing[0]}->{landing[0]} ms (v1) {up_landing[63]}->{landing[63]} ms (v64)"
                 if up_landing else "")
        glitch = f"  (ignored a failed strike at {', '.join(f'{format_duty(d)}%' for d in m['cut_short'])})" \
            if m["cut_short"] else ""
        if m.get("min_raised_from") is not None:
            glitch += f"  (min raised from {format_duty(m['min_raised_from'])}% to land in time)"
        print(f"key {key:2d}: min {format_duty(up['min'])}->{format_duty(m['min'])}%, duty "
              f"{format_duty(up['duties'][0])}->{format_duty(m['duties'][0])}% (v1) "
              f"{format_duty(up['duties'][63])}->{format_duty(m['duties'][63])}% (v64) "
              f"{format_duty(up['duties'][126])}->{format_duty(m['duties'][126])}% (v127){lands}{glitch}")
    return entries


def draft_pedal_clamps(mapping: dict, pedal: dict, cfg) -> None:
    """For keys clamped with the pedal up but not yet in PEDAL_BOUNCE_CLAMP: the
    same sets, starting at the pedal-down duty of the velocity where each set
    starts with the pedal up. A starting point to check by ear."""
    lines = []
    for key in sorted(cfg.BOUNCE_CLAMP):
        if key not in pedal or key not in mapping or key in cfg.PEDAL_BOUNCE_CLAMP:
            continue
        sets = cfg.BOUNCE_CLAMP[key]
        sets = sorted([sets] if isinstance(sets, tuple) else sets)
        up, down = mapping[key]["duties"], pedal[key]["duty"]
        drafted = []
        for above, offset, ms, duty in sets:
            first = next((v for v, d in enumerate(up) if d > above), None)  # 0-based velocity
            if first is None:
                continue  # never reached
            start = 0 if first == 0 else round(down[first] - cfg.DUTY_RESOLUTION, 2)
            drafted.append(f"({start:g}, {offset}, {ms}, {duty})")
        if drafted:
            lines.append(f"    {key}: [{', '.join(drafted)}],")
    if lines:
        print("\nA draft PEDAL_BOUNCE_CLAMP for keys clamped with the pedal up, starting each set at the same "
              "velocity (check each by ear with `listen.py crescendo <key> --pedal`):")
        print("\n".join(lines))


def write_table(path: Path, header: dict, entries: dict[int, dict]) -> None:
    """One key per line, so a change shows up clearly in a diff."""
    lines = ["{"] + [f"  {json.dumps(k)}: {json.dumps(v)}," for k, v in header.items()] + ['  "keys": {']
    keys = sorted(entries)
    for i, key in enumerate(keys):
        comma = "," if i < len(keys) - 1 else ""
        lines.append(f'    "{key}": {json.dumps(entries[key], separators=(",", ":"))}{comma}')
    lines += ["  }", "}"]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n")


def update_drive(args) -> None:
    """table --drive-only: rewrites just the drive settings in an existing
    table (its "drive" and each key's own), from the settings as they are now.
    Duties and landing times stay as they were; nothing is analysed."""
    if not args.out.exists():
        sys.exit(f"{args.out} doesn't exist yet: run table without --drive-only first")
    document = json.loads(args.out.read_text())
    header = {k: v for k, v in document.items() if k != "keys"}
    header["drive"] = drive_settings(args.cfg)
    header["drive_updated"] = time.strftime("%Y-%m-%d %H:%M")
    entries, changed = {}, []
    for name, old in document["keys"].items():
        key = int(name)
        rest = {k: v for k, v in old.items() if k not in KEY_DRIVE_FIELDS}
        entries[key] = {"duty": rest.pop("duty"), **key_drive(key, args.cfg), **rest}
        if {k: v for k, v in old.items() if k in KEY_DRIVE_FIELDS} != key_drive(key, args.cfg):
            changed.append(key)
        if "pedal" in old:
            pedal = {k: v for k, v in old["pedal"].items() if k not in KEY_DRIVE_FIELDS}
            own = key_drive(key, args.cfg, pedal=True)
            entries[key]["pedal"] = {"duty": pedal.pop("duty"), "landing_ms": pedal.pop("landing_ms"), **own, **pedal}
            if {k: v for k, v in old["pedal"].items() if k in KEY_DRIVE_FIELDS} != own and key not in changed:
                changed.append(key)
    write_table(args.out, header, entries)
    print(f"drive: {header['drive']}")
    print(f"keys whose own drive settings changed: {', '.join(map(str, changed)) or 'none'}")
    print(f"wrote {args.out}; upload it to the controller with: pio run -t uploadfs")
    print(OFFSETS_NOTE)


def table(args) -> None:
    """Writes the velocity table the controller plays from: for each calibrated
    key, the duty for every velocity, plus the measurements and settings it
    came from so a change can be traced. With --drive-only, see update_drive."""
    if args.drive_only:
        update_drive(args)
        return
    flags = load_flags(args.flags)
    by_key = load_by_key(args.raw, args.recordings, args.margin_db, flags, args.cfg.IGNORE_FIRST_STRIKE,
                         pedal=args.pedal)
    mapping = build_mapping(by_key, args, flags)
    print_key_log(by_key, mapping)

    own = {key: own_landing(m) for key, m in mapping.items()}
    up_latest = max((max(v) for v in own.values() if v), default=None)
    pedal = pedal_entries(args, mapping, up_latest)
    draft_pedal_clamps(mapping, pedal, args.cfg)

    def entry(key: int, m: dict) -> dict:
        return {
            "duty": m["duties"],
            # The key's own drive settings, where they differ from the table's "drive".
            **key_drive(key, args.cfg),
            **({"flags": m["by_ear"]} if m["by_ear"] else {}),
            **({"landing_ms": own[key]} if own[key] is not None else {}),
            "min": m["min"],
            "max": m["max"],
            "min_db": m["min_db"],
            "loud_db": round(m["loud_db"], 2),
            "smooth_soft_db": round(m["smooth_soft_db"], 2),
            "smooth_loud_db": round(m["smooth_loud_db"], 2),
            "offset_db": round(m["offset_db"], 2),
            "curve": m["curve"],
            **({"pedal": pedal[key]} if key in pedal else {}),
        }

    header = {
        "generated": time.strftime("%Y-%m-%d %H:%M"),
        "source": args.raw.name,
        "settings": {
            "neighbour_keys": args.neighbour_keys,
            "smoothing": args.smoothing,
            "tilt_db": args.tilt_db,
            "key_trims_db": {str(k): v for k, v in sorted(args.cfg.KEY_TRIMS_DB.items())},
        },
        # How the controller drives each press (see drive_command in calibration_common.py).
        "drive": drive_settings(args.cfg),
    }
    # Keys with their own landing times use them; the rest use a shared curve:
    # the median of the keys that have their own, or else the flight test's.
    with_own = [key for key in sorted(own) if own[key] is not None]
    if with_own:
        shared = [int(np.ceil(v)) for v in np.median([own[k] for k in with_own], axis=0)]
        header["landing_source"] = f"{args.raw.name}: {len(with_own)} keys' own; median of them for the rest"
        print(f"landing times: {len(with_own)} keys have their own; the rest use their median")
    else:
        flight = flight_test_landing(mapping, args.landing)
        shared = flight[0] if flight else None
        if flight:
            header["landing_source"] = f"{args.landing.name}, keys {', '.join(map(str, flight[1]))}, for every key"
            print(f"landing times: from the flight test of keys {', '.join(map(str, flight[1]))}, for every key")
        else:
            print("no landing times (calibrate with landing times, or run hold_tests.py flight)")
    if shared is not None:
        header["landing_ms"] = shared
        print(f"  shared curve: {shared[0]} ms at velocity 1, {shared[63]} ms at 64, {shared[-1]} ms at 127")
    write_table(args.out, header, {key: entry(key, m) for key, m in mapping.items()})
    print(f"wrote {args.out} ({len(mapping)} keys, {len(pedal)} with the pedal down too; "
          "others play the default mapping)")
    print("upload it to the controller with: pio run -t uploadfs")
    print(OFFSETS_NOTE)


def report(args) -> None:
    """Writes a page of charts of the calibration and the velocity mapping
    (calibration_report.html filled with this data) and opens it."""
    flags = load_flags(args.flags)
    by_key = load_by_key(args.raw, args.recordings, args.margin_db, flags, args.cfg.IGNORE_FIRST_STRIKE,
                         pedal=args.pedal)
    mapping = build_mapping(by_key, args, flags)
    levels_out, keys_out = [], []
    for key, levels in sorted(by_key.items()):
        m = mapping.get(key)
        if m is None:
            print(f"key {key:2d}: never played reliably, left out")
            continue
        curve = dict(m["curve"])
        for duty in sorted(levels):
            hits = [r["peak_db"] - r["gain_db"] for r in levels[duty] if r["hit"]]
            levels_out.append(
                {
                    "key": key,
                    "note": note_name(key),
                    "duty": duty,
                    "strikes": len(levels[duty]),
                    "hits": len(hits),
                    "level_db": round(statistics.median(hits), 2) if hits else None,
                    "curve_db": curve.get(duty),
                    "reliable": int(len(hits) == len(levels[duty])),
                }
            )
        own_v64 = m["min_db"] + 63 / 126 * (m["loud_db"] - m["min_db"])
        keys_out.append(
            {
                "key": key,
                "note": note_name(key),
                "threshold_duty": m["threshold"],
                "min_duty": m["min"],
                "max_duty": m["max"],
                "soft_db": m["soft_db"],
                "min_db": m["min_db"],
                "loud_db": round(m["loud_db"], 2),
                "range_db": round(m["loud_db"] - m["min_db"], 2),
                "gain_db": levels[min(levels)][0]["gain_db"],
                "cut_short": " ".join(f"{d}%" for d in m["cut_short"]),
                "smooth_soft_db": round(m["smooth_soft_db"], 2),
                "smooth_loud_db": round(m["smooth_loud_db"], 2),
                "offset_db": round(m["offset_db"], 2),
                # What velocity 64 plays: mapped onto the key's own range, and as the table maps it.
                "v64_own_db": round(own_v64, 2),
                "v64_mapped_db": round(level_at_duty(m, m["duties"][63]), 2),
                "v64_duty": m["duties"][63],
                "duties": m["duties"],
            }
        )

    template = (Path(__file__).resolve().parent / "calibration_report.html").read_text()
    data = {
        "levels": levels_out,
        "keys": keys_out,
        "source": str(args.raw),
        "generated": time.strftime("%Y-%m-%d %H:%M"),
        "settings": {
            "neighbour_keys": args.neighbour_keys,
            "smoothing": args.smoothing,
            "tilt_db": args.tilt_db,
            "trims": len(args.cfg.KEY_TRIMS_DB),
        },
    }
    # "</" would end the <script> the data sits in.
    page = template.replace(
        "__CALIBRATION_DATA__", json.dumps(data).replace("</", "<\\/")
    )
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(page)
    print(f"wrote {args.out} ({len(keys_out)} keys)")
    if args.open:
        import webbrowser

        webbrowser.open(args.out.resolve().as_uri())

