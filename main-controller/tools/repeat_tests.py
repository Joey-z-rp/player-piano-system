#!/usr/bin/env python3
"""How soon can each key strike again? On an upright the key has to come
most of the way back up before the hammer can strike again, and a loud
note's hammer takes longer to settle, so how soon a key can repeat is
measured rather than assumed: each test key is played twice in a row, at a
spread of intervals and velocities, exactly as the controller plays a repeat.

  measure  play the pairs (USB, pedal up; saves after each key, with every
           recording)
  review   listen to every recorded pair and say whether you hear one note
           or two (a page served from this computer)
  report   print, for each key, which intervals played both notes, using your
           verdicts where you've given them

The connection and microphone settings are in calibration_common.py.
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
import time
from pathlib import Path

import numpy as np
from scipy import signal

sys.path.insert(0, str(Path(__file__).resolve().parent))
import calibration_common as common  # noqa: E402

# === SETTINGS ===============================================================

# Each pair is played as the controller would play it (both presses
# from the velocity table, each sent early by its landing time), with the
# interval from the first note sounding to the second. The first note is
# released as the roll builder does ("gap", the default): restrike_gap_ms
# before the second press, but no sooner than min_note_ms after its hammer
# lands (both from the controller's config), so close repeats are released
# early and longer ones held (the review of keys 20 and 35 found holding
# better there). "early" releases it min_note_ms after it lands every time. A pair whose second press would come
# before the first's release can't be played that way, and is skipped.
# Most fast repeats in the pieces played (88% of those under 250 ms) fall on
# keys 22-65, so those are measured every 3-4 keys (as well as 20, 35, 50 and
# 65, measured first), and the ends sparsely.
KEYS = "0,10,24,27,30,32,38,41,44,46,51,54,58,60,62,72"
FIRSTS = [32, 64, 112]  # first notes' velocities...
SECONDS = [32, 48, 64, 88, 112]  # ...each with every second note's (the softest are left out: the volume avoids them)
# The intervals tried (ms between the two notes sounding), for each second
# velocity: around where it starts to work, from the keys measured so far.
INTERVALS = {
    32: list(range(120, 226, 15)),
    48: list(range(90, 196, 15)),
    64: list(range(90, 196, 15)),
    88: list(range(60, 151, 15)),
    112: list(range(60, 151, 15)),
}
STRIKES = 2  # per combination
HOLD_MS = 300  # the second note is held this long after it lands
# A second note: within WINDOW_BEFORE_MS before to WINDOW_AFTER_MS after when its
# hammer should land, the key's pitch jumps JUMP_DB within JUMP_MS, and is still
# 3 dB above where it jumped from SUSTAIN_MS later (a knock doesn't last). Tuned
# against the review of key 50 (95% agreement).
JUMP_DB = 4
JUMP_MS = 25
WINDOW_BEFORE_MS = 40
WINDOW_AFTER_MS = 60
SUSTAIN_MS = 40
LOWEST_HZ = 250  # partials listened to start above this: a press's knock is lower
RAW_FILE = common.PROJECT_DIR / "calibration" / "repeat_tests.json"
RECORDING_DIR = common.PROJECT_DIR / "calibration" / "repeat_recordings"
FLAGS_FILE = common.PROJECT_DIR / "calibration" / "repeat_flags.json"  # your verdicts (review)

# ============================================================================


def record_sequence(ctx, commands: list[tuple[float, str]]) -> tuple[np.ndarray, list[int]]:
    """Plays the commands (see play_sequence) while recording. Returns the
    audio and the sample each command went out at."""
    seconds = (common.LEAD_IN_MS + common.PRE_ROLL_MS + commands[-1][0] + common.TAIL_MS) / 1000
    sent = []

    def during():
        time.sleep(common.LEAD_IN_MS / 1000)
        sent.extend(ctx.piano.play_sequence(commands))

    audio, sample_at = ctx.timed.record(seconds, during)
    return audio, [sample_at(t) for t in sent]


def pitch_levels(audio: np.ndarray, rate: int, key: int) -> np.ndarray:
    """The level (dB, one per LANDING_STEP_MS) of a few of the key's own
    partials: a note on that key shows there, while mechanical noise and other
    strings ringing mostly don't."""
    f0 = 27.5 * 2 ** (key / 12)
    power = np.zeros(len(audio) // int(rate * common.LANDING_STEP_MS / 1000))
    # Four partials above LOWEST_HZ: the press's knock is lower than that.
    partials = [n * f0 for n in range(1, 40) if n * f0 >= LOWEST_HZ][:4]
    for f in partials:
        if f > min(5000, rate / 2 * 0.9):
            break
        sos = signal.butter(2, [f * 0.97, f * 1.03], btype="bandpass", fs=rate, output="sos")
        power += 10 ** (common.levels_1ms(signal.sosfilt(sos, audio), rate) / 10)
    return 10 * np.log10(np.maximum(power, 1e-20))


def repeat_command(key: int, velocity: int, table: dict, clamp: tuple) -> str:
    """The press for a repeated note with --clamp-repeats: the table's, but
    clamped with clamp (offset, ms, duty) in place of any clamp the key has."""
    entry = table["keys"][str(key)]
    drive = {**common.DEFAULT_DRIVE, **table.get("drive", {})}
    landing = entry.get("landing_ms") or table.get("landing_ms")
    return common.drive_command(key, entry["duty"][velocity - 1], landing[velocity - 1], drive, entry,
                                repeat_clamp=clamp)


def second_note(audio: np.ndarray, rate: int, sos, key: int, press1: int, press2: int, landing2: int) -> dict:
    """Looks for a note from the press at sample press2 (see JUMP_DB). press1 is
    the first press's sample; times returned are ms after it."""
    step = common.LANDING_STEP_MS
    pitch = pitch_levels(audio, rate, key)
    broad = common.levels_1ms(signal.sosfilt(sos, audio), rate)
    first = press1 * 1000 // rate // step
    press = (press2 - press1) * 1000 / rate
    noise = float(np.median(pitch[max(first - 140 // step, 0): first - 10 // step]))
    expected = press + landing2
    begin = first + int(max(expected - WINDOW_BEFORE_MS, press + 10) // step)
    end = min(first + int((expected + WINDOW_AFTER_MS) // step), len(pitch) - (SUSTAIN_MS + 30) // step)
    look = JUMP_MS // step
    for i in range(begin, end):
        before = float(pitch[max(i - look, 0): i].min())
        if pitch[i] - before < JUMP_DB or pitch[i] < noise + common.MARGIN_DB:
            continue
        later = float(np.median(pitch[i + SUSTAIN_MS // step: i + (SUSTAIN_MS + 30) // step]))
        if later < before + 3:
            continue  # didn't keep sounding
        peak = float(broad[i: i + 60 // step].max())
        return {"sounded": True, "lands_ms": round((i - first) * step - press), "peak_db": round(peak, 2),
                "press_ms": round(press, 1)}
    return {"sounded": False, "lands_ms": None, "peak_db": None, "press_ms": round(press, 1)}


def first_press(audio: np.ndarray, rate: int, key: int, landing1: int) -> int:
    """Where the first press went out, for pairs recorded before that was saved:
    its note's attack, less its landing time. The audio's latency puts it
    11-16 ms after where it was due."""
    due = (common.LEAD_IN_MS + common.PRE_ROLL_MS) * rate // 1000
    pitch = pitch_levels(audio, rate, key)
    noise = float(np.median(pitch[20: due * 1000 // rate - 10]))
    after = np.nonzero(pitch[due * 1000 // rate:] > noise + 15)[0]
    attack = (after[0] + due * 1000 // rate) if len(after) else due * 1000 // rate + landing1 + 14
    return int(np.clip(attack - landing1, due * 1000 // rate + 5, due * 1000 // rate + 25)) * rate // 1000


def redetect(rows: list[dict], folder: Path, table: dict) -> None:
    """Judges every recorded pair again with the current detection (and the
    single notes it's compared with), so changes to it apply to earlier
    measurements too."""
    for key, release in {(r["key"], r["release"]) for r in rows if "recording" in r}:
        path = folder / f"key_{key:02d}_{release}.npz"
        if not path.exists():
            continue
        saved = np.load(path)
        rate = int(saved["sample_rate"])
        sos = common.a_weighting(rate)
        for row in rows:
            if row["key"] != key or row["release"] != release or "recording" not in row:
                continue
            audio = saved["audio"][row["recording"]].astype(np.float32) / 32767
            if "press_samples" in row:
                press1, press2 = row["press_samples"]
            else:
                press1 = first_press(audio, rate, key, common.table_landing(key, row["first"], table))
                press2 = press1 + round(row["press2_ms"] * rate / 1000)
            found = second_note(audio, rate, sos, key, press1, press2, row["landing2_ms"])
            row["result"] = "both" if found["sounded"] else "missed"
            row["lands2_ms"] = found["lands_ms"]
            if row.get("alone_db") is not None or "level2_db" in row:
                alone = row.get("alone_db")
                row["level2_db"] = (round(found["peak_db"] - alone, 1)
                                    if found["sounded"] and alone is not None else row.get("level2_db"))


def repeat_key(key: int, table: dict, ctx, args) -> tuple[list[dict], list[np.ndarray]]:
    rows, recordings = [], []
    rate = ctx.args.sample_rate
    config = ctx.piano.config()
    min_hold_ms, release_gap_ms = config["min_note_ms"], config["restrike_gap_ms"]
    print(f"\n== key {key} ({common.note_name(key)}) ==")

    def quiet():
        common.wait_until_quiet(ctx.recorder, ctx.sos, ctx.room_db, common.QUIET_DB, common.MAX_QUIET_WAIT_S)

    # Each velocity alone, so a second note's loudness can be compared with it.
    singles = {}
    for velocity in sorted(set(args.firsts) | set(args.seconds)):
        peaks = []
        landing = common.table_landing(key, velocity, table)
        for _ in range(STRIKES):
            quiet()
            audio, starts = record_sequence(ctx, [(0, common.table_command(key, velocity, table)),
                                                  (landing + HOLD_MS, f"R:{key}:0")])
            found = second_note(audio, rate, ctx.sos, key, starts[0], starts[0], landing)
            if found["sounded"]:
                peaks.append(found["peak_db"])
        singles[velocity] = statistics.median(peaks) if peaks else None
        print(f"  alone at velocity {velocity}: {'silent' if not peaks else f'{singles[velocity]:.1f} dB'}")

    plan = []
    for first in args.firsts:
        for second in args.seconds:
            intervals = args.intervals or INTERVALS.get(second) or sorted({i for v in INTERVALS.values() for i in v})
            for interval in intervals:
                landing1, landing2 = common.table_landing(key, first, table), common.table_landing(key, second, table)
                press2 = landing1 + interval - landing2
                release1 = landing1 + min_hold_ms
                if args.release.startswith("gap"):
                    release1 = max(release1, press2 - release_gap_ms)
                if press2 < release1 + 5:
                    rows.append({"key": key, "first": first, "second": second, "interval": interval,
                                 "release": args.mode, "result": "can't schedule"})
                    continue
                plan.append((first, second, interval, landing1, landing2, press2, release1))
    skipped = sum(r["result"] == "can't schedule" for r in rows)
    print(f"  {len(plan)} combinations, {len(plan) * STRIKES} pairs; {skipped} can't be scheduled")
    # Interleaved, so slow drift affects every combination alike.
    for round_number in range(STRIKES):
        for first, second, interval, landing1, landing2, press2, release1 in plan:
            quiet()
            commands = [(0, common.table_command(key, first, table)), (release1, f"R:{key}:0"),
                        (press2, repeat_command(key, second, table, args.repeat_clamp) if args.repeat_clamp
                         else common.table_command(key, second, table)),
                        (press2 + landing2 + HOLD_MS, f"R:{key}:0")]
            audio, starts = record_sequence(ctx, commands)
            found = second_note(audio, rate, ctx.sos, key, starts[0], starts[2], landing2)
            level = (round(found["peak_db"] - singles[second], 1)
                     if found["sounded"] and singles.get(second) is not None else None)
            rows.append({"key": key, "first": first, "second": second, "interval": interval, "release": args.mode,
                         "result": "both" if found["sounded"] else "missed", "round": round_number,
                         "lands2_ms": found["lands_ms"], "landing2_ms": landing2, "level2_db": level,
                         "release1_ms": release1, "press2_ms": found["press_ms"], "recording": len(recordings),
                         "press_samples": [starts[0], starts[2]], "alone_db": singles.get(second),
                         "commands": [c for _, c in commands]})
            recordings.append(audio)
            print(f"  v{first:<3d}-> v{second:<3d} {interval:4d} ms: "
                  + (f"both, second {level:+.1f} dB vs alone, landed {found['lands_ms']} ms after its press "
                     f"(table {landing2})" if found["sounded"] else "SECOND MISSING")
                  + f"   released {release1} ms, pressed again {found['press_ms']:.0f} ms")
    return rows, recordings


def measure(args) -> None:
    table = json.loads(common.TABLE_FILE.read_text())
    piano = common.connect(args)
    table = common.with_offsets(table, piano.offsets())  # as the controller plays the keys
    if args.clamp_repeats and args.repeat_clamp is None:
        args.repeat_clamp = common.repeat_clamp(piano.config())  # as the controller plays repeats
    # How the results are filed: each way of playing the repeats apart.
    args.mode = args.release
    if args.repeat_clamp is not None:
        args.mode += "-clamp-" + "_".join(f"{v:g}" for v in args.repeat_clamp)
    done = {r["key"] for r in load(args.raw) if r["release"] == args.mode}
    keys = [k for k in common.parse_keys(args.keys) if args.redo or k not in done]
    missing = [k for k in keys if str(k) not in table["keys"]]
    if missing:
        sys.exit(f"keys {missing} aren't in {common.TABLE_FILE}")
    if not keys:
        print(f"Every key asked for is already in {args.raw} (use --redo to measure it again)")
        return
    if args.connection != "usb":
        print("Over Wi-Fi the timing between the two notes isn't reliable: use USB for this test")
    ctx = common.Session(args, piano)
    try:
        for key in keys:
            rows, recordings = repeat_key(key, table, ctx, args)
            kept = [r for r in load(args.raw) if not (r["key"] == key and r["release"] == args.mode)]
            args.raw.parent.mkdir(parents=True, exist_ok=True)
            temp = args.raw.with_suffix(".tmp")
            temp.write_text('{"repeat": [\n' + ",\n".join(json.dumps(r) for r in kept + rows) + "\n]}\n")
            temp.replace(args.raw)
            args.recordings.mkdir(parents=True, exist_ok=True)
            length = max(len(a) for a in recordings)
            audio = np.zeros((len(recordings), length), dtype=np.int16)
            for i, a in enumerate(recordings):
                audio[i, : len(a)] = np.round(np.clip(a, -1, 1) * 32767)
            np.savez_compressed(args.recordings / f"key_{key:02d}_{args.mode}.npz", audio=audio,
                                sample_rate=args.sample_rate)
    finally:
        ctx.piano.release_everything()


def load(path: Path) -> list[dict]:
    if not path.exists():
        return []
    return json.loads(path.read_text())["repeat"]


def report(args) -> None:
    rows = load(args.raw)
    if not rows:
        sys.exit(f"no measurements in {args.raw}: run `measure` first")
    redetect(rows, args.recordings, json.loads(common.TABLE_FILE.read_text()))
    flags = load_flags(args.flags)
    reviewed = [r for r in rows if flags.get(pair_name(r), {}).get("verdict")]
    if reviewed:
        agree = sum(judged(r, flags) == r["result"] for r in reviewed)
        missed_notes = sum(r["result"] == "missed" and judged(r, flags) == "both" for r in reviewed)
        false_notes = sum(r["result"] == "both" and judged(r, flags) == "missed" for r in reviewed)
        print(f"You reviewed {len(reviewed)} pairs: the detection agreed on {agree} ({agree / len(reviewed):.0%}); "
              f"it missed a second note you heard {missed_notes} times, and heard one you didn't {false_notes} times. "
              "Your verdicts are used below.\n")
    rows = [{**r, "result": judged(r, flags)} for r in rows]
    print("For each key, first -> second velocity, and each interval (ms between the notes sounding):\n"
          "  # both notes every time   + sometimes   . second note missing   - can't be scheduled that close")
    for key, release in sorted({(r["key"], r["release"]) for r in rows}):
        mine = [r for r in rows if r["key"] == key and r["release"] == release]
        intervals = sorted({r["interval"] for r in mine})
        firsts = sorted({r["first"] for r in mine})
        seconds = sorted({r["second"] for r in mine})
        print(f"\nkey {key} ({common.note_name(key)}), first note released "
              f"{'as soon as it lands' if release.startswith('early') else 'as the roll builder does now'}"
              f"{', every repeat clamped' if release.endswith('-clamp') else ''}")
        print("            " + "".join(f"{i:>4d}" for i in intervals) + "   shortest reliable   second note vs alone")
        for first in firsts:
            for second in seconds:
                cells, shortest = [], None
                for interval in intervals:
                    tries = [r for r in mine if r["first"] == first and r["second"] == second and r["interval"] == interval]
                    if not tries or tries[0]["result"] == "can't schedule":
                        cells.append("   -")
                        continue
                    both = sum(r["result"] == "both" for r in tries)
                    cells.append("   #" if both == len(tries) else "   +" if both else "   .")
                    if both == len(tries) and shortest is None:
                        # Reliable from here up?
                        later = [r for r in mine if r["first"] == first and r["second"] == second
                                 and r["interval"] >= interval and r["result"] != "can't schedule"]
                        if all(r["result"] == "both" for r in later):
                            shortest = interval
                levels = [r["level2_db"] for r in mine if r["first"] == first and r["second"] == second
                          and r.get("level2_db") is not None]
                level = f"{statistics.median(levels):+.1f} dB" if levels else "-"
                print(f"  v{first:<3d}-> v{second:<3d}" + "".join(cells)
                      + f"   {f'{shortest} ms' if shortest else 'none':>9s}          {level}")
        noted = [r for r in mine if flags.get(pair_name(r), {}).get("comment")]
        if noted:
            print("  your comments:")
            for r in sorted(noted, key=lambda r: (r["first"], r["second"], r["interval"], r.get("round", 0))):
                print(f"    v{r['first']} -> v{r['second']}, {r['interval']} ms, try {r.get('round', 0) + 1}: "
                      f"{flags[pair_name(r)]['comment']}")


def load_flags(path: Path) -> dict:
    """Your verdicts and comments from review, for each pair:
    {"50:early:12": {"verdict": "two" or "one", "comment": "..."}}."""
    flags = json.loads(path.read_text()) if path.exists() else {}
    # Saved before comments were added: just the verdict.
    return {name: {"verdict": v} if isinstance(v, str) else v for name, v in flags.items()}


def pair_name(row: dict) -> str:
    return f"{row['key']}:{row['release']}:{row.get('recording')}"


def judged(row: dict, flags: dict) -> str:
    """A pair's result: your verdict where you've given one, else the detection's."""
    verdict = flags.get(pair_name(row), {}).get("verdict")
    if verdict == "two":
        return "both"
    if verdict == "one":
        return "missed"
    return row["result"]


def review(args) -> None:
    """Serves a page (review.html) for listening to every recorded pair
    and saying whether you hear one note or two. report then uses your
    verdicts, and says how often the detection agreed with them."""
    import http.server
    import io
    import wave
    import webbrowser
    from urllib.parse import parse_qs, urlparse

    rows = [r for r in load(args.raw) if r["result"] != "can't schedule"]
    if not rows:
        sys.exit(f"no recorded pairs in {args.raw}: run `measure` first")
    print("Judging the recordings with the current detection...", flush=True)
    redetect(rows, args.recordings, json.loads(common.TABLE_FILE.read_text()))
    page = (Path(__file__).resolve().parent / "repeat_review.html").read_bytes()
    recordings: dict = {}

    def wav(key: int, release: str, index: int) -> bytes:
        name = (key, release)
        if name not in recordings:
            saved = np.load(args.recordings / f"key_{key:02d}_{release}.npz")
            recordings[name] = (saved["audio"], int(saved["sample_rate"]))
        audio, rate = recordings[name]
        out = io.BytesIO()
        with wave.open(out, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(rate)
            w.writeframes(audio[index].tobytes())
        return out.getvalue()

    class Handler(http.server.BaseHTTPRequestHandler):
        def send(self, status: int, body: bytes, kind: str) -> None:
            self.send_response(status)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:
            url = urlparse(self.path)
            query = {k: v[0] for k, v in parse_qs(url.query).items()}
            try:
                if url.path == "/":
                    self.send(200, page, "text/html; charset=utf-8")
                elif url.path == "/data":
                    data = {"rows": rows, "flags": load_flags(args.flags)}
                    self.send(200, json.dumps(data).encode(), "application/json")
                elif url.path == "/audio":
                    self.send(200, wav(int(query["key"]), query["release"], int(query["i"])), "audio/wav")
                else:
                    self.send(404, b"not found", "text/plain")
            except (KeyError, ValueError, FileNotFoundError, IndexError) as error:
                self.send(404, str(error).encode(), "text/plain")

        def do_POST(self) -> None:
            body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
            flags = load_flags(args.flags)
            name = f"{int(body['key'])}:{body['release']}:{int(body['recording'])}"
            entry = flags.get(name, {})
            if "verdict" in body:  # "two", "one", or null to clear it
                entry["verdict"] = body["verdict"] if body["verdict"] in ("two", "one") else None
            if "comment" in body:
                entry["comment"] = str(body["comment"] or "").strip()
            entry = {k: v for k, v in entry.items() if v}
            if entry:
                flags[name] = entry
            else:
                flags.pop(name, None)
            args.flags.parent.mkdir(parents=True, exist_ok=True)
            temp = args.flags.with_suffix(".tmp")
            temp.write_text(json.dumps(flags, indent=1, sort_keys=True) + "\n")
            temp.replace(args.flags)
            self.send(200, json.dumps(flags).encode(), "application/json")

        def log_message(self, *_args) -> None:
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    url = f"http://127.0.0.1:{args.port}/"
    print(f"Review page at {url} (verdicts are saved to {args.flags}); Ctrl-C to stop", flush=True)
    if args.open:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)

    p = commands.add_parser("measure", help="find how soon each test key can strike again")
    p.add_argument("--keys", default=KEYS)
    p.add_argument("--firsts", type=lambda v: [int(x) for x in v.split(",")], default=FIRSTS,
                   help="first notes' velocities, e.g. 32,64,112")
    p.add_argument("--seconds", type=lambda v: [int(x) for x in v.split(",")], default=SECONDS,
                   help="second notes' velocities, e.g. 32,48,64,88,112")
    p.add_argument("--intervals", type=lambda v: [int(x) for x in v.split(",")], default=None,
                   help="ms between the notes sounding, the same for every second velocity, e.g. 80,100,120 "
                        "(default: INTERVALS)")
    p.add_argument("--release", choices=["early", "gap"], default="gap",
                   help="release the first note as soon as it lands (early), or as the roll builder does now (gap)")
    p.add_argument("--clamp-repeats", action="store_true",
                   help="clamp every second note with the controller's repeat clamp, as it plays repeats "
                        "(filed apart from the rest)")
    p.add_argument("--repeat-clamp", type=lambda v: tuple(float(x) for x in v.split(",")), default=None,
                   help="clamp every second note with this OFFSET,MS,DUTY instead, e.g. =-20,100,100 "
                        "(filed apart too)")
    p.add_argument("--redo", action="store_true", help="measure again, replacing earlier results")
    p.add_argument("--raw", type=Path, default=RAW_FILE)
    p.add_argument("--recordings", type=Path, default=RECORDING_DIR)
    common.add_session_args(p)
    p.set_defaults(run=measure)

    p = commands.add_parser("review", help="listen to the recorded pairs and say how many notes you hear")
    p.add_argument("--raw", type=Path, default=RAW_FILE)
    p.add_argument("--recordings", type=Path, default=RECORDING_DIR)
    p.add_argument("--flags", type=Path, default=FLAGS_FILE)
    p.add_argument("--port", type=int, default=8766)
    p.add_argument("--no-open", dest="open", action="store_false")
    p.set_defaults(run=review)

    p = commands.add_parser("report", help="print which intervals played both notes")
    p.add_argument("--raw", type=Path, default=RAW_FILE)
    p.add_argument("--recordings", type=Path, default=RECORDING_DIR)
    p.add_argument("--flags", type=Path, default=FLAGS_FILE)
    p.set_defaults(run=report)

    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
