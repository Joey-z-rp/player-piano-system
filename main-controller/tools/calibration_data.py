"""The calibration's data: raw.csv (one row per strike), each key's recordings,
re-analysing strikes from their recordings, and the flags set on the review
page. Used by calibrate_dynamics.py and its other modules."""

from __future__ import annotations

import csv
import json
import statistics
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from calibration_common import MARGIN_DB, a_weighting, analyse_strike, format_duty, is_note  # noqa: E402


def drive_settings(cfg) -> dict:
    """The table's "drive": calibrate_dynamics.py's drive settings."""
    return {"strike_ms": cfg.STRIKE_MS, "coast_duty": cfg.COAST_DUTY, "coast_offset_ms": cfg.COAST_OFFSET_MS,
            "clamp_offset_ms": cfg.CLAMP_OFFSET_MS, "clamp_ms": cfg.CLAMP_MS, "clamp_duty": cfg.CLAMP_DUTY,
            "hold_duty": cfg.HOLD_DUTY}


RAW_FIELDS = [
    "key",
    "duty",
    "repeat",
    "hit",
    "peak_db",
    "noise_db",
    "sample_peak_db",
    "gain_db",
    "lands_ms",
    "bounce",
    "decay_db",
    "command",
]


def load_raw(path: Path) -> list[dict]:
    if not path.exists():
        return []
    with path.open() as f:
        return [
            {
                "key": int(r["key"]),
                "duty": float(r["duty"]),
                "repeat": int(r["repeat"]),
                "hit": r["hit"] == "1",
                "peak_db": float(r["peak_db"]),
                "noise_db": float(r["noise_db"]),
                # Not recorded before clipping checks were added.
                "sample_peak_db": float(r.get("sample_peak_db") or "nan"),
                "gain_db": float(
                    r.get("gain_db") or 0
                ),  # 0 before gain tagging was added
                # Not recorded before the drive coasted until the hammer landed.
                "lands_ms": int(r["lands_ms"]) if r.get("lands_ms") else None,
                "bounce": r.get("bounce") == "1",
                "decay_db": float(r.get("decay_db") or "nan"),
                "command": r.get("command") or "",
            }
            for r in csv.DictReader(f)
        ]


def save_key(path: Path, key: int, rows: list[dict]) -> None:
    """Writes one key's measurements, replacing any earlier ones for that key.
    The file is replaced in one step, so an interrupted run never loses data."""
    kept = [row for row in load_raw(path) if row["key"] != key]
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(".tmp")
    with temp.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=RAW_FIELDS, extrasaction="ignore")  # recordings are saved apart
        writer.writeheader()
        for row in kept + rows:
            writer.writerow(
                {
                    **row,
                    "duty": format_duty(row["duty"]),
                    "hit": int(row["hit"]),
                    "peak_db": f"{row['peak_db']:.2f}",
                    "noise_db": f"{row['noise_db']:.2f}",
                    "sample_peak_db": ""
                    if np.isnan(row["sample_peak_db"])
                    else f"{row['sample_peak_db']:.2f}",
                    "gain_db": f"{row['gain_db']:g}",
                    "lands_ms": "" if row["lands_ms"] is None else row["lands_ms"],
                    "bounce": int(row["bounce"]),
                    "decay_db": ""
                    if np.isnan(row["decay_db"])
                    else f"{row['decay_db']:.2f}",
                }
            )
    temp.replace(path)


def save_recordings(folder: Path, key: int, rows: list[dict], sample_rate: int) -> None:
    """One key's strike recordings (16-bit), replacing any earlier ones. Each
    starts PRE_ROLL_MS before its press."""
    folder.mkdir(parents=True, exist_ok=True)
    length = max(len(r["audio"]) for r in rows)
    audio = np.zeros((len(rows), length), dtype=np.int16)
    for i, row in enumerate(rows):
        audio[i, : len(row["audio"])] = np.round(np.clip(row["audio"], -1, 1) * 32767)
    np.savez_compressed(
        folder / f"key_{key:02d}.npz",
        repeat=np.array([r["repeat"] for r in rows]),
        audio=audio,
        sample_rate=sample_rate,
    )


def reanalyse(rows: list[dict], folder: Path, margin_db: float, later_onset: bool = False) -> int:
    """Measures each strike again from its recording, so every key with
    recordings is judged by the current analysis. Returns how many strikes'
    results changed. later_onset for the pedal-down measurements (see
    measure_strike)."""
    changed = 0
    by_key = defaultdict(list)
    for row in rows:
        by_key[row["key"]].append(row)
    for key, key_rows in by_key.items():
        path = folder / f"key_{key:02d}.npz"
        if not path.exists():
            continue
        saved = np.load(path)
        rate = int(saved["sample_rate"])
        sos = a_weighting(rate)
        audio = dict(zip(saved["repeat"].tolist(), saved["audio"]))
        for row in key_rows:
            recording = audio.get(row["repeat"])
            if recording is None:
                continue
            measured = analyse_strike(recording.astype(np.float32) / 32767, rate, sos, margin_db, key, later_onset)
            before = (row["hit"], row["lands_ms"] if row["hit"] else None, row["peak_db"])
            row.update(
                result=measured.result,
                hit=measured.result == "hit",
                peak_db=measured.peak_db,
                noise_db=measured.noise_db,
                lands_ms=measured.lands_ms,
                bounce=measured.bounce,
                decay_db=measured.decay_db,
            )
            # The 16-bit recording moves levels by ~0.005 dB: not a change.
            after = (row["hit"], row["lands_ms"] if row["hit"] else None)
            changed += before[:2] != after or abs(before[2] - row["peak_db"]) > 0.05
    return changed


MAX_SETTING_STEPS = 6  # as the controller allows (include/app/piano/velocity_table.h)


def bounce_clamps(table: dict) -> tuple[dict, dict, dict]:
    """BOUNCE_CLAMP ({key: (above, offset, ms, duty) or a list of them}) as
    the per-key KEY_CLAMP_MS, KEY_CLAMP_OFFSET_MS and KEY_CLAMP_DUTY, each in
    steps by strike duty: each set from its `above` up to the next set's, and
    no clamp below the first."""
    clamp_ms, offset, duty = {}, {}, {}
    for key, sets in table.items():
        if not sets:
            continue
        sets = sorted([sets] if isinstance(sets, tuple) else list(sets))
        aboves = [s[0] for s in sets]
        if len(set(aboves)) != len(aboves):
            sys.exit(f"BOUNCE_CLAMP key {key}: two sets start at the same duty")
        first = sets[0]
        below = [] if first[0] == 0 else [(0, None)]  # no clamp below the first set
        if len(below) + len(sets) > MAX_SETTING_STEPS:
            sys.exit(f"BOUNCE_CLAMP key {key}: at most {MAX_SETTING_STEPS - len(below)} sets")
        steps = below + [(s[0], s) for s in sets]
        clamp_ms[key] = [(a, 0 if s is None else s[2]) for a, s in steps]
        # Below the first set there's no clamp, so its offset and duty don't matter: carry the first set's.
        offset[key] = [(a, (s or first)[1]) for a, s in steps]
        duty[key] = [(a, (s or first)[3]) for a, s in steps]
    return clamp_ms, offset, duty


def key_drive(key: int, cfg, pedal: bool = False) -> dict:
    """A key's own drive settings for its table entry, where they differ from
    the drive: "coast", "coast_offset_ms", "clamp_offset_ms", "clamp_ms",
    "clamp_duty" and "hold", all but "coast" either one value or [[strike
    duty above which, value], ...]. See KEY_* in calibrate_dynamics.py. With
    the pedal down, its clamp comes from PEDAL_BOUNCE_CLAMP instead."""
    clamp_offset = cfg.PEDAL_KEY_CLAMP_OFFSET_MS if pedal else cfg.KEY_CLAMP_OFFSET_MS
    clamp_ms = cfg.PEDAL_KEY_CLAMP_MS if pedal else cfg.KEY_CLAMP_MS
    clamp_duty = cfg.PEDAL_KEY_CLAMP_DUTY if pedal else cfg.KEY_CLAMP_DUTY
    # (entry field, per-key setting, default, a time in whole ms)
    fields = [("coast", cfg.KEY_COAST_DUTY, cfg.COAST_DUTY, False),
              ("coast_offset_ms", cfg.KEY_COAST_OFFSET_MS, cfg.COAST_OFFSET_MS, True),
              ("clamp_offset_ms", clamp_offset, cfg.CLAMP_OFFSET_MS, True),
              ("clamp_ms", clamp_ms, cfg.CLAMP_MS, True),
              ("clamp_duty", clamp_duty, cfg.CLAMP_DUTY, False),
              ("hold", cfg.KEY_HOLD_DUTY, cfg.HOLD_DUTY, False)]
    own = {}
    for name, per_key, default, whole_ms in fields:
        value = per_key.get(key, default)
        steps = [(0, value)] if isinstance(value, (int, float)) else sorted(value)
        steps = [(float(a), round(v) if whole_ms else float(v)) for a, v in steps]
        steps = [step for i, step in enumerate(steps) if i == 0 or step[1] != steps[i - 1][1]]  # merge repeats
        if steps != [(0, round(default) if whole_ms else float(default))]:
            if name == "coast" and len(steps) > 1:
                sys.exit(f"key {key}: KEY_COAST_DUTY is one duty, not steps (the coast sets how loud it plays)")
            own[name] = steps[0][1] if len(steps) == 1 else [[a, v] for a, v in steps]
    return own


def load_flags(path: Path) -> dict:
    """Your verdicts from the review page: {"keys": {"50": {"damped": true,
    "bounces": false, "comment": "..."}}, "strikes": {"30:12": "thump"}}."""
    flags = json.loads(path.read_text()) if path.exists() else {}
    return {"keys": flags.get("keys", {}), "strikes": flags.get("strikes", {})}


def load_by_key(path: Path, recordings: Path | None = None, margin_db: float = MARGIN_DB,
                flags: dict | None = None, ignore_first_strike: bool = True,
                pedal: bool = False) -> dict[int, dict[int, list[dict]]]:
    """Strikes by key and duty (see group_rows), re-analysed from their
    recordings. pedal: measured with the pedal down, so judged as such (see
    measure_strike's later_onset, and drop_thumps)."""
    rows = load_raw(path)
    if not rows:
        sys.exit(f"no measurements in {path}")
    if recordings is not None:
        changed = reanalyse(rows, recordings, margin_db, pedal)
        if changed:
            print(f"{changed} strike(s) re-analysed from their recordings differ from when they were measured")
    by_key = group_rows(rows, flags, ignore_first_strike)
    if pedal:
        dropped = drop_thumps(by_key, flags)
        if dropped:
            print(f"{len(dropped)} strike(s) taken as the press's thump ringing the open strings, not notes")
    return by_key


# A pedal-down "note" landing under this fraction of the time the same key's
# notes take 3-15% duty higher up (see drop_thumps).
THUMP_LANDING_FRACTION = 0.6


def drop_thumps(by_key: dict[int, dict[float, list[dict]]], flags: dict | None = None) -> set[tuple[int, int]]:
    """With the pedal down no string is damped, so a press's thump can set the
    low strings ringing long enough to pass as a note: within ~10 ms of the
    press, at their pitch rather than the key's. A note can't land sooner than
    the same key does at a higher duty (a lower duty is a slower stroke), so a
    hit landing under THUMP_LANDING_FRACTION of the key's landing 3-15% higher
    up is counted as a miss, unless you've judged it on the review page.
    Returns those strikes, as (key, repeat)."""
    judged = (flags or {}).get("strikes", {})
    dropped = set()
    for key, levels in by_key.items():
        lands = {d: statistics.median(ls) for d, rows in levels.items()
                 if (ls := [r["lands_ms"] for r in rows if r["hit"] and r["lands_ms"] is not None])}
        for duty, rows in levels.items():
            above = [ms for d, ms in lands.items() if duty + 3 <= d <= duty + 15]
            if not above:
                continue
            limit = THUMP_LANDING_FRACTION * statistics.median(above)
            for i, row in enumerate(rows):
                if (row["hit"] and row["lands_ms"] is not None and row["lands_ms"] < limit
                        and f"{key}:{row['repeat']}" not in judged):
                    rows[i] = {**row, "hit": False, "result": "thump"}
                    dropped.add((key, row["repeat"]))
    return dropped


def group_rows(rows: list[dict], flags: dict | None = None,
               ignore_first_strike: bool = True) -> dict[int, dict[int, list[dict]]]:
    """Strikes by key and duty, as the analysis uses them: without each key's
    first strike (see IGNORE_FIRST_STRIKE), and with your verdicts applied."""
    first = {}
    for row in rows:
        first[row["key"]] = min(first.get(row["key"], row["repeat"]), row["repeat"])
    by_key: dict[int, dict[int, list[dict]]] = defaultdict(lambda: defaultdict(list))
    for row in rows:
        if ignore_first_strike and row["command"] and row["repeat"] == first[row["key"]]:
            continue
        if row["command"] and row["hit"] and not is_note(row["lands_ms"]):
            # Measured before thumps were told apart from notes: judge it now.
            row = {**row, "hit": False}
        verdict = (flags or {}).get("strikes", {}).get(f"{row['key']}:{row['repeat']}")
        if verdict in ("note", "thump"):
            row = {**row, "hit": verdict == "note"}
        by_key[row["key"]][row["duty"]].append(row)
    return by_key

