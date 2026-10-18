"""Measuring: striking each key across its duties and recording every strike
(calibrate_dynamics.py measure), plus the level and clipcheck commands. The
settings come from calibrate_dynamics.py's SETTINGS block, as args.cfg."""

from __future__ import annotations

import json
import statistics
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from calibration_common import (  # noqa: E402
    CLIP_DBFS,
    MAX_QUIET_WAIT_S,
    PRE_ROLL_MS,
    QUIET_DB,
    set_pedal,
    TAIL_MS,
    Connection,
    Recorder,
    TimedRecorder,
    a_weighting,
    analyse_strike,
    connect,
    format_duty,
    measure_room,
    measure_strike,
    note_name,
    parse_keys,
    record_press,
    saturation_start,
    wait_until_quiet,
)
from calibration_common import drive_command  # noqa: E402
from calibration_data import drive_settings, key_drive, load_raw, save_key, save_recordings  # noqa: E402


def measure(args) -> None:
    keys = parse_keys(args.keys)
    done = {row["key"] for row in load_raw(args.raw)}
    if args.redo:
        todo = keys
        redone = sorted(done & set(keys))
        if redone:
            print(
                f"Re-measuring {len(redone)} keys; each key's old results are replaced once it finishes"
            )
    else:
        todo = [key for key in keys if key not in done]
        if done & set(keys):
            print(
                f"Skipping {len(keys) - len(todo)} keys already in {args.raw} "
                "(use --redo to measure them again)"
            )
    if not todo:
        return

    strike_s = (PRE_ROLL_MS + args.hold_ms + args.tail_ms) / 1000
    levels_per_key = (
        args.soft_range / args.soft_step
        + args.fine_range / args.fine_step
        + (100 - args.start) / args.step
    )
    # Plus about half a second per strike waiting for quiet; more on bass keys.
    estimate = len(todo) * levels_per_key * 3.5 * (strike_s + 0.5)
    print(
        f"{len(todo)} keys: at most ~{estimate / 3600:.1f} h, "
        "usually much less since sweeps stop at saturation"
    )

    starts = {}
    if args.from_table and args.table.exists():
        table_keys = json.loads(args.table.read_text())["keys"]
        # With the pedal down a key starts lower: from its pedal entry, else searched for.
        entries = {k: v.get("pedal", {}) if args.pedal else v for k, v in table_keys.items()}
        starts = {int(k): float(v["min"]) for k, v in entries.items() if "min" in v}
    if args.pedal:
        print(f"With the sustain pedal down for every strike; saving to {args.raw} and {args.recordings}")
    searched = [key for key in todo if key not in starts]
    if searched:
        print(f"Searching live for where these keys start to play: {', '.join(map(str, searched))}")

    piano = connect(args)
    recorder = Recorder(args.device, args.sample_rate)  # for waiting until quiet
    timed = TimedRecorder(args.device, args.sample_rate)  # for the strikes
    sos = a_weighting(args.sample_rate)
    room_db = measure_room(recorder, sos)

    for key in todo:
        # A fresh start for every key, so a driver stuck in a bad state can't
        # spoil a key's measurements.
        piano.reset_drivers()
        rows = measure_key(key, args, piano, recorder, timed, sos, room_db, starts.get(key))
        save_key(args.raw, key, rows)
        save_recordings(args.recordings, key, rows, args.sample_rate)
        clipped = sum(1 for r in rows if r["sample_peak_db"] > CLIP_DBFS)
        if clipped:
            print(
                f"  !! {clipped} strikes clipped the microphone: turn its gain down and redo this key"
            )


def press_command(key: int, duty: float, starts: float | None, cfg, pedal: bool = False) -> str:
    """How to strike this key at this duty: as the controller plays it (see
    drive_command), taking the landing time as the latest the hammer should
    land (see LANDING_LIMIT) for a key that starts to play at `starts`.
    Without `starts`, at the coast duty all the way to the release."""
    entry = key_drive(key, cfg, pedal)
    if starts is None:
        return f"P:{key}:{format_duty(duty)}:{cfg.STRIKE_MS}:0:0:{format_duty(entry.get('coast', cfg.COAST_DUTY))}"
    above, latest = zip(*cfg.LANDING_LIMIT)
    landing = float(np.interp(max(duty - starts, 0), above, latest))
    return drive_command(key, duty, landing, drive_settings(cfg), entry)


def frange(start: float, stop: float, step: float) -> list[float]:
    """start, start + step, ... up to and including stop, rounded to 0.01."""
    count = int(round((stop - start) / step)) + 1
    return [round(start + i * step, 2) for i in range(max(count, 0))]


def measure_key(
    key: int,
    args,
    piano: Connection,
    recorder: Recorder,
    timed: TimedRecorder,
    sos: np.ndarray,
    room_db: float,
    known_start: float | None = None,
) -> list[dict]:
    """Finds where the key starts to sound, one strike per duty: in DUTY_STEP
    steps, then in FINE_STEP steps below the first duty that sounded. Then
    sweeps upward from just below there: SOFT_STEP steps for SOFT_RANGE, then
    FINE_STEP up to FINE_RANGE past the first sounding duty, then DUTY_STEP,
    until the loudness stops rising."""
    cfg = args.cfg
    rows: list[dict] = []
    reference: list[float] = []  # where the key starts to play, once the search has found it
    counts: dict[
        tuple[str, float], int
    ] = {}  # strikes so far per (search or sweep, duty), for the log
    print(f"\n== key {key} ({note_name(key)}) ==")

    def strike(duty: float, label: str = "strike") -> dict:
        press = press_command(key, duty, reference[0] if reference else None, cfg, args.pedal)
        while True:
            waited, quiet = wait_until_quiet(
                recorder, sos, room_db, args.quiet_db, args.max_quiet_wait
            )
            if not quiet:
                print(
                    f"  duty {format_duty(duty):>5s}  still not quiet after {waited:.0f} s (is the key still ringing?), "
                    "striking anyway"
                )
            elif waited > 2:
                print(
                    f"  duty {format_duty(duty):>5s}  waited {waited:.1f} s for the last note to die away"
                )
            if args.pedal:
                set_pedal(piano, True)  # waits until it's down and quiet again
            try:
                recorded = record_press(timed, piano, press, f"R:{key}:0", args.hold_ms)
            finally:
                if args.pedal:
                    set_pedal(piano, False)  # the dampers stop the note, so the next wait is short
            if recorded is None:
                print(
                    f"  duty {format_duty(duty):>5s}  network hiccup during this strike, doing it again"
                )
                continue
            measured = analyse_strike(recorded[0], args.sample_rate, sos, args.margin_db, key, args.pedal)
            # Something made a sound just before the strike (or the note never died away).
            if quiet and measured.noise_db > room_db + args.quiet_db + 3:
                print(
                    f"  duty {format_duty(duty):>5s}  background too loud before this strike "
                    f"({measured.noise_db:.1f} dB), doing it again"
                )
                continue
            break
        row = {
            "key": key,
            "duty": duty,
            "repeat": len(rows),
            "hit": measured.result == "hit",
            "peak_db": measured.peak_db,
            "noise_db": measured.noise_db,
            "sample_peak_db": measured.sample_peak_db,
            "gain_db": args.gain_db,
            "lands_ms": measured.lands_ms,
            "bounce": measured.bounce,
            "decay_db": measured.decay_db,
            "command": press,
            "audio": recorded[0],
        }
        rows.append(row)
        counts[(label, duty)] = counts.get((label, duty), 0) + 1
        number = counts[(label, duty)]
        lands = f"lands {measured.lands_ms:3d} ms" if measured.lands_ms is not None else ""
        flags = "  CLIPPED" if row["sample_peak_db"] > CLIP_DBFS else ""
        flags += "  BOUNCE" if measured.bounce else ""
        print(
            f"  duty {format_duty(duty):>5s}  {label} {number}: {measured.result:9s}  {measured.peak_db:6.1f} dB  "
            f"{lands:14s} (noise {measured.noise_db:6.1f} dB)  {press}{flags}"
        )
        return row

    for _ in range(cfg.WARM_UP_PRESSES):
        wait_until_quiet(recorder, sos, room_db, args.quiet_db, args.max_quiet_wait)
        coast = key_drive(key, cfg).get("coast", cfg.COAST_DUTY)
        piano.play(f"P:{key}:{cfg.WARM_UP_DUTY}:{cfg.STRIKE_MS}:0:0:{format_duty(coast)}", f"R:{key}:0", args.hold_ms)
    if cfg.WARM_UP_PRESSES:
        print(f"  {cfg.WARM_UP_PRESSES} warm-up press(es) at {cfg.WARM_UP_DUTY}%, not recorded")

    if known_start is not None:
        # 1. Where the key starts to play, from the velocity table.
        starts = first_sound = known_start
        soft_from = round(starts - args.below_start, 2)
        print(f"  starts to play at {format_duty(starts)}% in {args.table.name}")
    else:
        # 1. Search: one strike per DUTY_STEP until the key makes a sound, then one
        #    per FINE_STEP below that to see how low it starts.
        first_sound = None
        for duty in frange(args.start, 100, args.step):
            if strike(duty, "search")["hit"]:
                first_sound = duty
                break
        if first_sound is None:
            print("  never sounded, even at 100%")
            return rows
        if first_sound == args.start:
            print(
                f"  already sounds at the start duty {args.start}: lower START_DUTY to find the true minimum"
            )
        starts = first_sound
        for duty in frange(max(args.start, first_sound - args.step + args.fine_step), first_sound - args.fine_step,
                           args.fine_step):
            if strike(duty, "search")["hit"]:
                starts = duty
                break
        soft_from = round(starts - args.fine_step + args.soft_step, 2)

    # Coast as if the key starts as high as it might (see LANDING_LIMIT).
    reference.append(round(starts + (cfg.START_MAY_BE_ABOVE if known_start is not None else 0), 2))
    soft_from = max(soft_from, 1)
    soft_to = min(starts + args.soft_range, 100)
    duties = frange(soft_from, soft_to, args.soft_step)
    fine_to = min(max(first_sound + args.fine_range, soft_to), 100)
    duties += frange(soft_to + args.fine_step, fine_to, args.fine_step)
    duties += frange(duties[-1] + args.step, 100, args.step)
    duties = [d for d in duties if d <= 100]
    print(
        f"  {format_duty(args.soft_step)}% steps from "
        f"{format_duty(soft_from)}% to {format_duty(soft_to)}%, {format_duty(args.fine_step)}% steps to "
        f"{format_duty(fine_to)}%, then {format_duty(args.step)}% steps"
    )

    # 2. Sweep.
    reliable_levels = 0
    curve: list[tuple[float, float]] = []
    for duty in duties:
        level = [strike(duty) for _ in range(3)]
        hits = sum(r["hit"] for r in level)
        if hits == 0:
            print(f"  duty {format_duty(duty):>5s}  -> no hits")
            continue
        # Near the threshold, "does it always play" needs more strikes.
        if reliable_levels < args.low_levels:
            print(f"  duty {format_duty(duty):>5s}  -> near the threshold, 2 more strikes")
            level += [strike(duty) for _ in range(2)]
        peaks = [r["peak_db"] for r in level if r["hit"]]
        if (
            hits == len(level)
            and len(level) == 3
            and max(peaks) - min(peaks) > args.spread_db
        ):
            print(
                f"  duty {format_duty(duty):>5s}  -> strikes differ by {max(peaks) - min(peaks):.1f} dB, 2 more strikes"
            )
            level += [strike(duty) for _ in range(2)]
            peaks = [r["peak_db"] for r in level if r["hit"]]

        hits = sum(r["hit"] for r in level)
        median = statistics.median(peaks)
        gain = (
            f", {median - curve[-1][1]:+.1f} dB vs last level"
            if curve and hits == len(level)
            else ""
        )
        print(
            f"  duty {format_duty(duty):>5s}  -> {hits}/{len(level)} hits, median {median:6.1f} dB{gain}"
        )
        if hits == len(level):
            reliable_levels += 1
            curve.append((duty, median))
            if saturation_start(curve, args.step, args.flat_db) is not None:
                print(
                    f"  saturated: two {format_duty(args.step)}% spans in a row added under {args.flat_db} dB, moving on"
                )
                break
    return rows


def level(args) -> None:
    """Strikes one key at full duty and reports its peak, for setting the mic gain."""
    piano = connect(args)
    recorder = Recorder(args.device, args.sample_rate)
    sos = a_weighting(args.sample_rate)
    seconds = (PRE_ROLL_MS + args.hold_ms + TAIL_MS) / 1000
    room_db = measure_room(recorder, sos)
    for _ in range(3):
        wait_until_quiet(recorder, sos, room_db, QUIET_DB, MAX_QUIET_WAIT_S)
        clean = []
        audio = recorder.record(
            seconds,
            lambda: clean.append(piano.strike(args.key, args.duty, args.hold_ms)),
        )
        if not clean[0]:
            print("  network hiccup during this strike, skipping it")
            continue
        result, peak, noise = measure_strike(
            audio, args.sample_rate, sos, args.margin_db
        )
        raw_peak = 20 * np.log10(max(float(np.abs(audio).max()), 1e-10))
        print(
            f"key {args.key} ({note_name(args.key)}) duty {args.duty}: sample peak {raw_peak:5.1f} dBFS, "
            f"A-weighted level {peak:5.1f} dB, noise {noise:5.1f} dB, {result}"
        )
        if raw_peak > -1:
            print("  clipping: turn the mic gain down")


def clipcheck(args) -> None:
    """For keys measured before clipping was recorded: strikes each key twice at
    the loudest duty its sweep used and reports the raw peak. A key that stays
    below the limit there didn't clip anywhere in its sweep. Run this at the
    mic gain the keys were measured with."""
    measured: dict[int, float] = {}
    for row in load_raw(args.raw):
        measured[row["key"]] = max(measured.get(row["key"], 0), row["duty"])
    keys = [key for key in parse_keys(args.keys) if key in measured]
    if not keys:
        sys.exit(f"none of those keys are in {args.raw}")

    piano = connect(args)
    recorder = Recorder(args.device, args.sample_rate)
    sos = a_weighting(args.sample_rate)
    seconds = (PRE_ROLL_MS + args.hold_ms + TAIL_MS) / 1000
    room_db = measure_room(recorder, sos)

    redo = []
    for key in keys:
        duty = measured[key]
        peaks = []
        while len(peaks) < 2:
            wait_until_quiet(recorder, sos, room_db, QUIET_DB, MAX_QUIET_WAIT_S)
            clean = []
            audio = recorder.record(
                seconds, lambda: clean.append(piano.strike(key, duty, args.hold_ms))
            )
            if clean[0]:
                peaks.append(20 * np.log10(max(float(np.abs(audio).max()), 1e-10)))
        peak = max(peaks)
        if peak > CLIP_DBFS:
            verdict = "CLIPPED: redo"
            redo.append(key)
        elif peak > CLIP_DBFS - 2:
            verdict = "close to the limit: redo to be safe"
            redo.append(key)
        else:
            verdict = "ok, keep"
        print(
            f"key {key:2d} ({note_name(key):>3s}) at {format_duty(duty):>5s}%: raw peak {peak:5.1f} dBFS  {verdict}"
        )

    if redo:
        print(
            f"\nRedo at a lower mic gain: --keys {','.join(str(k) for k in redo)} --redo"
        )
    else:
        print("\nNo clipping: nothing to redo")


def check(args) -> None:
    """A rough check of the velocity table as the controller plays it: each
    key at a few velocities, with the exact press the controller sends (its
    landing times, clamp and hold), against the level the table expects from
    the curve it was built from. The table's curves were measured with presses
    timed from LANDING_LIMIT, so a clamp that starts before the landing comes
    sooner when played, and can play louder. A key's levels can all be off by
    the same amount between sessions (the microphone, the room), so each key's
    median difference is taken as that, and what's flagged is a velocity
    differing from it by over --flag-db, or a strike that didn't sound."""
    from listen import Table  # plays exactly what the controller would

    table = Table(args.table)
    table.pedal = args.pedal
    keys = [k for k in parse_keys(args.keys) if k in table.entries and (not args.pedal or "pedal" in table.entries[k])]
    if not keys:
        sys.exit("none of those keys are in the table" + (" with a pedal entry" if args.pedal else ""))
    strikes = len(keys) * len(args.velocities) * args.strikes
    print(f"{strikes} strikes on {len(keys)} keys{' with the pedal down' if args.pedal else ''}; "
          f"roughly {strikes * (4.5 if args.pedal else 3) / 60:.0f} minutes")

    piano = connect(args)
    recorder = Recorder(args.device, args.sample_rate)
    timed = TimedRecorder(args.device, args.sample_rate)
    sos = a_weighting(args.sample_rate)
    room_db = measure_room(recorder, sos)
    results = []
    try:
        for key in keys:
            entry = table.entry(key)
            curve_duties, curve_db = zip(*entry["curve"])
            print(f"\n== key {key} ({note_name(key)}) ==")
            for velocity in args.velocities:
                duty = table.duty(key, velocity)
                press = table.command(key, velocity)
                expected = float(np.interp(duty, curve_duties, curve_db))
                levels, lands, misses = [], [], 0
                while len(levels) + misses < args.strikes:
                    wait_until_quiet(recorder, sos, room_db, QUIET_DB, MAX_QUIET_WAIT_S)
                    if args.pedal:
                        set_pedal(piano, True)
                    try:
                        recorded = record_press(timed, piano, press, f"R:{key}:0", args.hold_ms)
                    finally:
                        if args.pedal:
                            set_pedal(piano, False)
                    if recorded is None:
                        continue
                    measured = analyse_strike(recorded[0], args.sample_rate, sos, args.margin_db, key, args.pedal)
                    if measured.result == "hit":
                        levels.append(measured.peak_db - args.gain_db)
                        if measured.lands_ms is not None:
                            lands.append(measured.lands_ms)
                    else:
                        misses += 1
                level = statistics.median(levels) if levels else None
                landing = (entry.get("landing_ms") or table.shared_landing or [None] * 127)[velocity - 1]
                results.append({"key": key, "velocity": velocity, "duty": duty, "expected_db": round(expected, 2),
                                "level_db": None if level is None else round(level, 2), "misses": misses,
                                "lands_ms": statistics.median(lands) if lands else None, "table_lands_ms": landing,
                                "command": press})
                got = "  DIDN'T SOUND" if level is None else f"{level:6.1f} dB ({level - expected:+5.1f})"
                heard = f"lands {statistics.median(lands):4.0f} ms (table {landing})" if lands else ""
                print(f"  v{velocity:<3d} {format_duty(duty):>6s}%  expected {expected:6.1f} dB, got {got}"
                      f"{f', {misses} missed' if misses and level is not None else ''}  {heard}  {press}")
    finally:
        piano.release_everything()
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(results, indent=1))

    print("\nEach key's median difference (the session's level, taken as no change), then velocities that "
          f"differ from it by over {args.flag_db} dB:")
    flagged = []
    for key in keys:
        rows = [r for r in results if r["key"] == key]
        diffs = [r["level_db"] - r["expected_db"] for r in rows if r["level_db"] is not None]
        if not diffs:
            continue
        offset = statistics.median(diffs)
        notes = [f"v{r['velocity']} {r['level_db'] - r['expected_db'] - offset:+.1f}" for r in rows
                 if r["level_db"] is not None and abs(r["level_db"] - r["expected_db"] - offset) > args.flag_db]
        notes += [f"v{r['velocity']} missed {r['misses']}/{args.strikes}" for r in rows if r["misses"]]
        if notes:
            flagged.append(key)
        print(f"  key {key:2d}: {offset:+5.1f} dB{'   ' + ', '.join(notes) if notes else ''}")
    print(f"\n{len(flagged)} of {len(keys)} keys flagged{': ' + ', '.join(map(str, flagged)) if flagged else ''}. "
          f"Saved to {args.out}")

