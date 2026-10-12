#!/usr/bin/env python3
"""Measure how loud each key plays at each duty cycle.

Strikes keys through the controller's /api/driver/command endpoint, records
each strike with a microphone, and measures its A-weighted peak level. The
results feed a per-key velocity -> duty mapping.

Settings live in the SETTINGS block below; any of them can be overridden
for one run on the command line (see --help on each command).

Steps:
  1. Plug the controller into this computer by USB (or set CONNECTION to
     "wifi" and set HOST), list input devices, pick a microphone without
     automatic gain control, and set DEVICE below:
       python3 tools/calibrate_dynamics.py devices
  2. Set the mic gain so the loudest key peaks around -6 dBFS:
       python3 tools/calibrate_dynamics.py level
  3. Measure (saves after each key; rerun the same command to resume):
       python3 tools/calibrate_dynamics.py measure
     To recalibrate some keys, replacing their earlier results:
       python3 tools/calibrate_dynamics.py measure --keys 39 --redo
  4. Build the velocity table the controller plays from (also prints each
     key's min duty, max duty and range, with any warnings), then upload it:
       python3 tools/calibrate_dynamics.py table
       pio run -t uploadfs
  5. See it all as charts in the browser (writes calibration/report.html):
       python3 tools/calibrate_dynamics.py report

Each key is first searched in DUTY_STEP steps, one strike each, for the duty
where it starts to sound. It's then swept in FINE_STEP steps around that
threshold and for FINE_RANGE above it, then in DUTY_STEP steps. Each level
gets 3 strikes, or 5 while the key hasn't yet played reliably, plus 2 more
when the 3 disagree by over SPREAD_DB. The sweep stops once two DUTY_STEP
spans in a row add under FLAT_DB.

Requires numpy and scipy; measuring also needs sounddevice, and pyserial
for a USB connection.
"""

from __future__ import annotations

import argparse
import csv
import json
import statistics
import sys
import time
import urllib.error
import urllib.request
from collections import defaultdict
from pathlib import Path

import numpy as np
from scipy import signal

# === SETTINGS ===============================================================

# Setup
# "usb": commands go over the controller's USB cable. Use this when you can:
# over Wi-Fi a delayed command can reach the controller late and spoil a
# later strike, which the script can't detect. Close the serial monitor first.
CONNECTION = "usb"
SERIAL_PORT = None  # None = find it automatically, or e.g. "/dev/cu.usbmodem1101"
HOST = None  # controller address, for CONNECTION = "wifi"
DEVICE = "Samson"  # input device number or name from `devices`; None = system default
SAMPLE_RATE = 48000
# The microphone gain relative to the first session, in dB. Every strike is
# saved with it, and levels are converted back to the first session's gain
# so keys measured at different gains can be compared. If you change the gain,
# remeasure a key or two that were measured before and set this to the average
# level difference (e.g. -4.0 if levels came out 4 dB lower).
MIC_GAIN_DB = -4.0

# Each strike
HOLD_MS = 300  # how long each key is held down
TAIL_MS = 500  # recorded after the release, so the whole attack is captured

# Before each strike, wait until the previous note has died away: within
# QUIET_DB of the room's background, measured at the start of each run.
QUIET_DB = 6
MAX_QUIET_WAIT_S = 10  # strike anyway after this long, with a warning

# Hit detection: lower MARGIN_DB if real notes are logged "too short" or
# "silent"; raise it if clicks are logged as hits.
MARGIN_DB = 10  # how far above the background noise a note must be

# Sweep
KEYS = "0-87"  # e.g. "0-87", "39", or "30-40,50"
START_DUTY = 25  # lowest duty tried
DUTY_STEP = (
    5  # step for finding where the key starts to sound, and above the fine region
)
FINE_STEP = 1  # step near the threshold, where loudness changes fastest
FINE_RANGE = 15  # how far above the first sounding duty to keep using FINE_STEP
LOW_LEVELS = 3  # the first reliable levels get 5 strikes instead of 3
SPREAD_DB = 1.5  # 3 strikes further apart than this get 2 more
FLAT_DB = 0.5  # two DUTY_STEP spans in a row adding less than this = saturated

# Analysis
MIN_MARGIN = 0  # added to the lowest reliable duty, for safety
# Occasionally a strike sounds at full strength but stops within 100 ms (the
# note is cut short rather than not struck). Above a key's threshold, one such
# miss per level, within CUT_SHORT_DB of that level's hits, is tolerated and
# listed instead of making the level count as unreliable.
CUT_SHORT_DB = 5

# Velocity mapping (table and report). Velocity picks a level between a key's
# softest and loudest, in dB; those ends are the median over the key and its
# neighbours, so neighbouring keys sound alike at the same velocity. A key
# that can't reach a level plays the nearest one it can.
NEIGHBOUR_KEYS = 4  # keys on each side the soft and loud levels are smoothed over
# How far each key's softest and loudest levels move towards its neighbours'
# median: 1 = all the way (neighbouring keys match), 0 = not at all (each key
# uses its own full range), 0.5 = halfway.
SMOOTHING = 1.0
TILT_DB = (
    0.0  # added to the lowest key, sliding to 0 at the highest: + makes the bass louder
)
KEY_TRIMS_DB: dict[
    int, float
] = {}  # per key, by ear: e.g. {64: 1.5} plays key 64 1.5 dB louder
# The drivers' PWM sets duty in 0.25% steps (400 steps per cycle at 8 MHz and
# 20 kHz), so the table uses those steps too.
DUTY_RESOLUTION = 0.25

# Files (relative to the main-controller folder)
PROJECT_DIR = Path(__file__).resolve().parent.parent
RAW_FILE = PROJECT_DIR / "calibration" / "raw.csv"
TABLE_FILE = (
    PROJECT_DIR / "data" / "velocity_table.json"
)  # uploaded with `pio run -t uploadfs`
REPORT_FILE = PROJECT_DIR / "calibration" / "report.html"

# ============================================================================

PIANO_KEYS = 88
WINDOW_MS = 10  # level is the loudest window of this length
ATTACK_MS = 150  # ...within this long after the onset
SUSTAIN_MS = (
    100  # a note still sounds this long after the onset; a solenoid click doesn't
)
PRE_ROLL_MS = 150  # silence recorded before each strike, for the noise floor
RAW_FIELDS = [
    "key",
    "duty",
    "repeat",
    "hit",
    "peak_db",
    "noise_db",
    "sample_peak_db",
    "gain_db",
]
CLIP_DBFS = (
    -1.0
)  # a strike whose raw peak is above this may have clipped the microphone


# --- audio -----------------------------------------------------------------


def a_weighting(sample_rate: int) -> np.ndarray:
    """IEC 61672 A-weighting as second-order sections."""
    f1, f2, f3, f4 = 20.598997, 107.65265, 737.86223, 12194.217
    zeros = [0, 0, 0, 0]
    poles = [-2 * np.pi * f for f in (f4, f4, f3, f2, f1, f1)]
    gain = (2 * np.pi * f4) ** 2 * 10 ** (1.9997 / 20)
    z, p, k = signal.bilinear_zpk(zeros, poles, gain, sample_rate)
    return signal.zpk2sos(z, p, k)


def window_levels_db(audio: np.ndarray, sample_rate: int) -> np.ndarray:
    """RMS level of consecutive WINDOW_MS windows, in dBFS."""
    size = int(sample_rate * WINDOW_MS / 1000)
    count = len(audio) // size
    windows = audio[: count * size].reshape(count, size)
    rms = np.sqrt(np.mean(windows**2, axis=1))
    return 20 * np.log10(np.maximum(rms, 1e-10))


def sliding_peak_db(audio: np.ndarray, sample_rate: int) -> float:
    """Loudest WINDOW_MS stretch, checked at every sample position. Fixed blocks
    would read up to ~2 dB differently on bass notes depending on how they
    happen to line up with the waveform."""
    size = int(sample_rate * WINDOW_MS / 1000)
    energy = np.concatenate([[0.0], np.cumsum(audio.astype(np.float64) ** 2)])
    rms = np.sqrt(np.max(energy[size:] - energy[:-size]) / size)
    return float(20 * np.log10(max(rms, 1e-10)))


def measure_strike(
    audio: np.ndarray, sample_rate: int, sos: np.ndarray, margin_db: float
):
    """Returns (result, peak_db, noise_db) for a recording that starts with PRE_ROLL_MS of
    silence. result is "hit", "silent", or "too short" (a click that didn't keep sounding)."""
    weighted = signal.sosfilt(sos, audio)
    levels = window_levels_db(weighted, sample_rate)
    pre = int(PRE_ROLL_MS / WINDOW_MS)
    # Skip the first window: the filter is still settling there.
    noise_db = float(np.median(levels[1:pre]))
    after = levels[pre:]
    loud = np.nonzero(after > noise_db + margin_db)[0]
    if len(loud) == 0:
        return "silent", float(after.max()), noise_db
    onset = loud[0]
    size = int(sample_rate * WINDOW_MS / 1000)
    start = (pre + onset - 1) * size  # from one window early, so the attack isn't cut
    peak_db = sliding_peak_db(
        weighted[start : start + int(sample_rate * (ATTACK_MS + WINDOW_MS) / 1000)],
        sample_rate,
    )
    later = after[onset + int(SUSTAIN_MS / WINDOW_MS) :][:3]
    sustained = len(later) > 0 and float(np.median(later)) > noise_db + margin_db
    return ("hit" if sustained else "too short"), peak_db, noise_db


class Recorder:
    def __init__(self, device, sample_rate: int):
        import sounddevice  # only needed when recording

        self.sd = sounddevice
        self.device = device
        self.sample_rate = sample_rate

    def record(self, seconds: float, during) -> np.ndarray:
        """Records for `seconds`, calling during() once recording has started."""
        frames = int(seconds * self.sample_rate)
        audio = self.sd.rec(
            frames,
            samplerate=self.sample_rate,
            channels=1,
            dtype="float32",
            device=self.device,
        )
        during()
        self.sd.wait()
        return audio[:, 0]


# --- controller --------------------------------------------------------------


class Connection:
    """How driver commands reach the controller."""

    def send(self, command: str) -> bool:
        """Sends a command. Returns True if it went through first time and promptly."""
        raise NotImplementedError

    def release_everything(self) -> None:
        raise NotImplementedError

    def strike(self, key: int, duty: int, hold_ms: int) -> bool:
        """Presses and releases a key. Returns False if the press was retried
        or late, in which case the strike can't be trusted and should be redone."""
        time.sleep(PRE_ROLL_MS / 1000)
        pressed_cleanly = self.send(f"P:{key}:{duty}")
        time.sleep(hold_ms / 1000)
        released_cleanly = self.send(f"R:{key}:0")
        if not (pressed_cleanly and released_cleanly):
            self.release_everything()  # in case anything was left held during the outage
        return pressed_cleanly


class WifiConnection(Connection):
    """Sends driver commands through the web API. Network failures are retried
    until they get through, so a dropout pauses the run instead of ending it."""

    RETRY_DELAYS_S = [0.5, 1, 2, 5, 10, 30]  # the last one repeats
    # A press acknowledged later than this may have landed too late in the
    # recording for its attack to be captured. Recordings run over a second,
    # and the measurement needs ~260 ms after the note starts.
    LATE_S = 0.5

    def __init__(self, host: str):
        self.base = f"http://{host}"

    def _post(self, path: str, body: dict | None = None) -> None:
        data = json.dumps(body or {}).encode()
        request = urllib.request.Request(
            self.base + path, data=data, headers={"Content-Type": "application/json"}
        )
        try:
            urllib.request.urlopen(request, timeout=2).read()
        except urllib.error.HTTPError as error:
            if error.code < 500:  # a rejected command is a bug, not the network
                sys.exit(
                    f"{path} {body}: controller replied {error.code} {error.read().decode(errors='replace')}"
                )
            raise

    def _post_until_sent(self, path: str, body: dict | None = None) -> bool:
        """Returns True if it went through first time and quickly."""
        started = time.monotonic()
        try:
            self._post(path, body)
            return time.monotonic() - started < self.LATE_S
        except (
            OSError,
            urllib.error.URLError,
        ) as error:  # timeouts, refused or dropped connections, 5xx
            problem = error
        for attempt in range(1, 1_000_000):
            delay = self.RETRY_DELAYS_S[min(attempt - 1, len(self.RETRY_DELAYS_S) - 1)]
            print(
                f"  network problem sending {body or path} ({problem}); retrying in {delay:g} s"
            )
            time.sleep(delay)
            try:
                self._post(path, body)
                print("  controller reachable again")
                return False
            except (OSError, urllib.error.URLError) as error:
                problem = error
        return False

    def send(self, command: str) -> bool:
        return self._post_until_sent("/api/driver/command", {"command": command})

    def release_everything(self) -> None:
        self._post_until_sent("/api/playback/stop")


class UsbConnection(Connection):
    """Sends driver commands over the controller's USB serial port (see
    src/serial_commands.cpp). Commands arrive in order and on time."""

    REPLY_TIMEOUT_S = 2.0

    def __init__(self, port: str | None):
        import serial  # only needed for USB
        from serial.tools import list_ports

        if port is None:
            found = [
                p.device
                for p in list_ports.comports()
                if "usbmodem" in p.device or "usbserial" in p.device
            ]
            if len(found) != 1:
                sys.exit(
                    "Couldn't pick the controller's USB port automatically "
                    f"(found: {', '.join(found) or 'none'}). Set SERIAL_PORT."
                )
            port = found[0]
        try:
            self.serial = serial.Serial(port, 115200, timeout=0.1)
        except serial.SerialException as error:
            sys.exit(f"Can't open {port} ({error}). Is the serial monitor still open?")
        print(f"Controller on {port}")

        # Opening the port can restart some boards: give it a few seconds to answer.
        deadline = time.monotonic() + 8
        while True:
            try:
                self._command("STOP")
                return
            except TimeoutError:
                if time.monotonic() > deadline:
                    sys.exit(
                        f"The controller didn't answer on {port}. Is its firmware up to date?"
                    )

    def _command(self, text: str) -> None:
        for _ in range(50):
            self.serial.reset_input_buffer()  # drop log output from earlier
            self.serial.write((text + "\n").encode())
            deadline = time.monotonic() + self.REPLY_TIMEOUT_S
            while time.monotonic() < deadline:
                line = self.serial.readline().decode(errors="replace").strip()
                if line == "#OK":
                    return
                if line.startswith("#ERR"):
                    reason = line[5:]
                    if reason == "queue full":
                        break  # the controller is catching up; try again
                    sys.exit(f"{text}: controller replied {reason}")
            else:
                raise TimeoutError(text)
            time.sleep(0.05)
        sys.exit(f"{text}: the controller's command queue stayed full")

    def send(self, command: str) -> bool:
        try:
            self._command(command)
        except TimeoutError:
            sys.exit(
                f"No reply to {command} over USB. Was the controller unplugged or restarted?"
            )
        return True

    def release_everything(self) -> None:
        self.send("STOP")


def connect(args) -> Connection:
    if args.connection == "usb":
        return UsbConnection(args.port)
    if not args.host:
        sys.exit(
            "Set HOST in the SETTINGS block at the top of this script, or pass --host"
        )
    return WifiConnection(args.host)


def note_name(key: int) -> str:
    """Piano key 0 is A0, key 39 is C4 (middle C)."""
    names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    note = key + 21
    return f"{names[note % 12]}{note // 12 - 1}"


def background_db(audio: np.ndarray, sample_rate: int, sos: np.ndarray) -> float:
    """Typical A-weighted level of a stretch of recording."""
    levels = window_levels_db(signal.sosfilt(sos, audio), sample_rate)
    # Skip the first window: the filter is still settling there.
    return float(np.median(levels[1:]))


def measure_room(recorder: Recorder, sos: np.ndarray) -> float:
    room_db = background_db(
        recorder.record(2.0, lambda: None), recorder.sample_rate, sos
    )
    print(
        f"Room background: {room_db:.1f} dB (keep the room this quiet while measuring)"
    )
    return room_db


def wait_until_quiet(
    recorder: Recorder,
    sos: np.ndarray,
    room_db: float,
    quiet_db: float,
    max_wait_s: float,
):
    """Listens until the level is back within quiet_db of the room. Returns
    (seconds waited, whether it got quiet)."""
    started = time.monotonic()
    while True:
        chunk = recorder.record(0.2, lambda: None)
        waited = time.monotonic() - started
        if background_db(chunk, recorder.sample_rate, sos) <= room_db + quiet_db:
            return waited, True
        if waited >= max_wait_s:
            return waited, False


# --- measure -----------------------------------------------------------------


def parse_keys(text: str) -> list[int]:
    keys: list[int] = []
    for part in text.split(","):
        first, _, last = part.partition("-")
        keys.extend(range(int(first), int(last or first) + 1))
    if not all(0 <= key < PIANO_KEYS for key in keys):
        sys.exit("keys must be 0-87")
    return keys


def load_raw(path: Path) -> list[dict]:
    if not path.exists():
        return []
    with path.open() as f:
        return [
            {
                "key": int(r["key"]),
                "duty": int(r["duty"]),
                "repeat": int(r["repeat"]),
                "hit": r["hit"] == "1",
                "peak_db": float(r["peak_db"]),
                "noise_db": float(r["noise_db"]),
                # Not recorded before clipping checks were added.
                "sample_peak_db": float(r.get("sample_peak_db") or "nan"),
                "gain_db": float(
                    r.get("gain_db") or 0
                ),  # 0 before gain tagging was added
            }
            for r in csv.DictReader(f)
        ]


def saturation_start(
    curve: list[tuple[int, float]], span: int, flat_db: float
) -> int | None:
    """First duty from which two successive `span`-wide stretches of duty each
    add under flat_db, or None. Gains are measured over a fixed span so that
    finely spaced points near the threshold don't look flat."""

    def gain_from(i: int):
        for j in range(i + 1, len(curve)):
            if curve[j][0] >= curve[i][0] + span:
                return j, (curve[j][1] - curve[i][1]) * span / (
                    curve[j][0] - curve[i][0]
                )
        return None, 0.0

    for i in range(len(curve)):
        j, first = gain_from(i)
        if j is None:
            return None
        k, second = gain_from(j)
        if k is None:
            return None
        if first < flat_db and second < flat_db:
            return curve[i][0]
    return None


def save_key(path: Path, key: int, rows: list[dict]) -> None:
    """Writes one key's measurements, replacing any earlier ones for that key.
    The file is replaced in one step, so an interrupted run never loses data."""
    kept = [row for row in load_raw(path) if row["key"] != key]
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(".tmp")
    with temp.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=RAW_FIELDS)
        writer.writeheader()
        for row in kept + rows:
            writer.writerow(
                {
                    **row,
                    "hit": int(row["hit"]),
                    "peak_db": f"{row['peak_db']:.2f}",
                    "noise_db": f"{row['noise_db']:.2f}",
                    "sample_peak_db": ""
                    if np.isnan(row["sample_peak_db"])
                    else f"{row['sample_peak_db']:.2f}",
                    "gain_db": f"{row['gain_db']:g}",
                }
            )
    temp.replace(path)


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
    levels_per_key = args.fine_range / args.fine_step + (100 - args.start) / args.step
    # Plus about half a second per strike waiting for quiet; more on bass keys.
    estimate = len(todo) * levels_per_key * 3.5 * (strike_s + 0.5)
    print(
        f"{len(todo)} keys: at most ~{estimate / 3600:.1f} h, "
        "usually much less since sweeps stop at saturation"
    )

    piano = connect(args)
    recorder = Recorder(args.device, args.sample_rate)
    sos = a_weighting(args.sample_rate)
    room_db = measure_room(recorder, sos)

    for key in todo:
        rows = measure_key(key, args, piano, recorder, sos, strike_s, room_db)
        save_key(args.raw, key, rows)
        clipped = sum(1 for r in rows if r["sample_peak_db"] > CLIP_DBFS)
        if clipped:
            print(
                f"  !! {clipped} strikes clipped the microphone: turn its gain down and redo this key"
            )


def measure_key(
    key: int,
    args,
    piano: Connection,
    recorder: Recorder,
    sos: np.ndarray,
    strike_s: float,
    room_db: float,
) -> list[dict]:
    """Finds where the key starts to sound with one strike per DUTY_STEP, then
    sweeps upward from just above the last silent level: FINE_STEP up to
    FINE_RANGE past the first sounding level, DUTY_STEP after that, until the
    loudness stops rising."""
    rows: list[dict] = []
    counts: dict[
        tuple[str, int], int
    ] = {}  # strikes so far per (search or sweep, duty), for the log
    print(f"\n== key {key} ({note_name(key)}) ==")

    def strike(duty: int, label: str = "strike") -> dict:
        while True:
            waited, quiet = wait_until_quiet(
                recorder, sos, room_db, args.quiet_db, args.max_quiet_wait
            )
            if not quiet:
                print(
                    f"  duty {duty:3d}  still not quiet after {waited:.0f} s (is the key still ringing?), "
                    "striking anyway"
                )
            elif waited > 2:
                print(
                    f"  duty {duty:3d}  waited {waited:.1f} s for the last note to die away"
                )
            clean = []
            audio = recorder.record(
                strike_s, lambda: clean.append(piano.strike(key, duty, args.hold_ms))
            )
            if not clean[0]:
                print(
                    f"  duty {duty:3d}  network hiccup during this strike, doing it again"
                )
                continue
            result, peak, noise = measure_strike(
                audio, args.sample_rate, sos, args.margin_db
            )
            # Something made a sound just before the strike (or the note never died away).
            if quiet and noise > room_db + args.quiet_db + 3:
                print(
                    f"  duty {duty:3d}  background too loud before this strike ({noise:.1f} dB), doing it again"
                )
                continue
            break
        row = {
            "key": key,
            "duty": duty,
            "repeat": len(rows),
            "hit": result == "hit",
            "peak_db": peak,
            "noise_db": noise,
            "sample_peak_db": 20 * np.log10(max(float(np.abs(audio).max()), 1e-10)),
            "gain_db": args.gain_db,
        }
        rows.append(row)
        counts[(label, duty)] = counts.get((label, duty), 0) + 1
        number = counts[(label, duty)]
        clipped = "  CLIPPED" if row["sample_peak_db"] > CLIP_DBFS else ""
        print(
            f"  duty {duty:3d}  {label} {number}: {result:9s}  {peak:6.1f} dB  (noise {noise:6.1f} dB){clipped}"
        )
        return row

    # 1. Search: one strike per DUTY_STEP until the key makes a sound.
    first_sound = None
    for duty in range(args.start, 101, args.step):
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
    fine_from = max(args.start, first_sound - args.step + args.fine_step)
    fine_to = min(first_sound + args.fine_range, 100)
    duties = list(range(fine_from, fine_to + 1, args.fine_step))
    duties += list(range(duties[-1] + args.step, 101, args.step))
    print(
        f"  first sound at {first_sound}%: {args.fine_step}% steps from {fine_from}% to {fine_to}%, "
        f"then {args.step}% steps"
    )

    # 2. Sweep.
    reliable_levels = 0
    curve: list[tuple[int, float]] = []
    for duty in duties:
        level = [strike(duty) for _ in range(3)]
        hits = sum(r["hit"] for r in level)
        if hits == 0:
            print(f"  duty {duty:3d}  -> no hits")
            continue
        # Near the threshold, "does it always play" needs more strikes.
        if reliable_levels < args.low_levels:
            print(f"  duty {duty:3d}  -> near the threshold, 2 more strikes")
            level += [strike(duty) for _ in range(2)]
        peaks = [r["peak_db"] for r in level if r["hit"]]
        if (
            hits == len(level)
            and len(level) == 3
            and max(peaks) - min(peaks) > args.spread_db
        ):
            print(
                f"  duty {duty:3d}  -> strikes differ by {max(peaks) - min(peaks):.1f} dB, 2 more strikes"
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
            f"  duty {duty:3d}  -> {hits}/{len(level)} hits, median {median:6.1f} dB{gain}"
        )
        if hits == len(level):
            reliable_levels += 1
            curve.append((duty, median))
            if saturation_start(curve, args.step, args.flat_db) is not None:
                print(
                    f"  saturated: two {args.step}% spans in a row added under {args.flat_db} dB, moving on"
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
    measured: dict[int, int] = {}
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
            f"key {key:2d} ({note_name(key):>3s}) at {duty:3d}%: raw peak {peak:5.1f} dBFS  {verdict}"
        )

    if redo:
        print(
            f"\nRedo at a lower mic gain: --keys {','.join(str(k) for k in redo)} --redo"
        )
    else:
        print("\nNo clipping: nothing to redo")


def devices(_args) -> None:
    import sounddevice

    print(sounddevice.query_devices())


# --- analysis ----------------------------------------------------------------


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


def summarize_key(levels: dict[int, list[dict]], args) -> dict | None:
    """Applies the analysis rules to one key's strikes, grouped by duty.
    Returns None if the key never played reliably."""
    duties = sorted(levels)
    reliable = [d for d in duties if all(r["hit"] for r in levels[d])]
    # min: lowest reliable duty with every level above it also reliable,
    # apart from single cut-short strikes.
    threshold = None
    cut_short = []
    for duty in reversed(duties):
        if duty in reliable:
            threshold = duty
        elif threshold is not None and is_cut_short(levels[duty], args.cut_short_db):
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

    # max: where two DUTY_STEP spans in a row add under --flat-db.
    max_duty = saturation_start(curve, args.step, args.flat_db) or curve[-1][0]
    min_duty = min(threshold + args.min_margin, max_duty)
    return {
        "key": next(iter(levels.values()))[0]["key"],
        "threshold": threshold,
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


def load_by_key(path: Path) -> dict[int, dict[int, list[dict]]]:
    rows = load_raw(path)
    if not rows:
        sys.exit(f"no measurements in {path}")
    by_key: dict[int, dict[int, list[dict]]] = defaultdict(lambda: defaultdict(list))
    for row in rows:
        by_key[row["key"]][row["duty"]].append(row)
    return by_key


def duty_for_level(summary: dict, target_db: float) -> float:
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
    return round(round(duty / DUTY_RESOLUTION) * DUTY_RESOLUTION, 2)


def level_at_duty(summary: dict, duty: float) -> float:
    curve = summary["curve"]
    return float(np.interp(duty, [d for d, _ in curve], [db for _, db in curve]))


def build_mapping(by_key: dict[int, dict[int, list[dict]]], args) -> dict[int, dict]:
    """Each key that plays reliably: its summary (see summarize_key), its
    neighbour-smoothed softest and loudest levels, and the duty for each
    velocity 1-127. The softest end is what a key plays at its min duty, not
    the softer level measured at its threshold, which it is never asked for.
    Each end moves SMOOTHING of the way from the key's own level towards the
    median of its neighbours'."""
    summaries = {}
    for key, levels in sorted(by_key.items()):
        summary = summarize_key(levels, args)
        if summary is not None:
            summaries[key] = summary

    mapping = {}
    for key, summary in summaries.items():
        near = [
            n for n in summaries.values() if abs(n["key"] - key) <= args.neighbour_keys
        ]
        soft = summary["min_db"] + args.smoothing * (statistics.median(n["min_db"] for n in near) - summary["min_db"])
        loud = summary["loud_db"] + args.smoothing * (statistics.median(n["loud_db"] for n in near) - summary["loud_db"])
        offset = args.tilt_db * (1 - key / (PIANO_KEYS - 1)) + KEY_TRIMS_DB.get(
            key, 0.0
        )
        duties = [
            duty_for_level(summary, soft + (v - 1) / 126 * (loud - soft) + offset)
            for v in range(1, 128)
        ]
        mapping[key] = {
            **summary,
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
        note = ""
        if m["cut_short"]:
            note = f"  (ignored a cut-short strike at {', '.join(f'{d}%' for d in m['cut_short'])})"
        if m["clipped"]:
            note += f"  !! CLIPPED in {m['clipped']} strikes: redo at a lower mic gain"
        print(
            f"key {key:2d}: min {m['min']:3d}  max {m['max']:3d}  "
            f"range {m['loud_db'] - m['min_db']:5.1f} dB  ({m['min_db']:.1f} to {m['loud_db']:.1f}){note}"
        )


def table(args) -> None:
    """Writes the velocity table the controller plays from: for each calibrated
    key, the duty for every velocity, plus the measurements and settings it
    came from so a change can be traced."""
    by_key = load_by_key(args.raw)
    mapping = build_mapping(by_key, args)
    print_key_log(by_key, mapping)

    def entry(m: dict) -> dict:
        return {
            "duty": m["duties"],
            "min": m["min"],
            "max": m["max"],
            "min_db": m["min_db"],
            "loud_db": round(m["loud_db"], 2),
            "smooth_soft_db": round(m["smooth_soft_db"], 2),
            "smooth_loud_db": round(m["smooth_loud_db"], 2),
            "offset_db": round(m["offset_db"], 2),
            "curve": m["curve"],
        }

    header = {
        "generated": time.strftime("%Y-%m-%d %H:%M"),
        "source": args.raw.name,
        "settings": {
            "neighbour_keys": args.neighbour_keys,
            "smoothing": args.smoothing,
            "tilt_db": args.tilt_db,
            "key_trims_db": {str(k): v for k, v in sorted(KEY_TRIMS_DB.items())},
        },
    }
    # One key per line, so a change shows up clearly in a diff.
    lines = (
        ["{"]
        + [f"  {json.dumps(k)}: {json.dumps(v)}," for k, v in header.items()]
        + ['  "keys": {']
    )
    keys = sorted(mapping)
    for i, key in enumerate(keys):
        comma = "," if i < len(keys) - 1 else ""
        lines.append(
            f'    "{key}": {json.dumps(entry(mapping[key]), separators=(",", ":"))}{comma}'
        )
    lines += ["  }", "}"]
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines) + "\n")
    print(f"wrote {args.out} ({len(mapping)} keys; others play the default mapping)")
    print("upload it to the controller with: pio run -t uploadfs")


def report(args) -> None:
    """Writes a page of charts of the calibration and the velocity mapping
    (calibration_report.html filled with this data) and opens it."""
    by_key = load_by_key(args.raw)
    mapping = build_mapping(by_key, args)
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
            "trims": len(KEY_TRIMS_DB),
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


# --- cli ---------------------------------------------------------------------


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    commands = parser.add_subparsers(dest="command", required=True)

    def add_audio(p):
        p.add_argument("--connection", choices=["usb", "wifi"], default=CONNECTION)
        p.add_argument(
            "--port", default=SERIAL_PORT, help="USB serial port, for --connection usb"
        )
        p.add_argument(
            "--host",
            default=HOST,
            help="controller IP address or hostname, for --connection wifi",
        )
        p.add_argument(
            "--device",
            default=DEVICE,
            type=lambda v: int(v) if v.isdigit() else v,
            help="input device (see `devices`)",
        )
        p.add_argument("--sample-rate", type=int, default=SAMPLE_RATE)
        p.add_argument(
            "--gain-db", type=float, default=MIC_GAIN_DB, help="see MIC_GAIN_DB"
        )
        p.add_argument(
            "--hold-ms", type=int, default=HOLD_MS, help="how long each key is held"
        )
        p.add_argument(
            "--margin-db",
            type=float,
            default=MARGIN_DB,
            help="level above noise that counts as a hit",
        )

    commands.add_parser("devices", help="list audio input devices").set_defaults(
        run=devices
    )

    p = commands.add_parser("level", help="strike one key to set the mic gain")
    add_audio(p)
    p.add_argument("--key", type=int, default=0)
    p.add_argument("--duty", type=int, default=100)
    p.set_defaults(run=level)

    p = commands.add_parser(
        "clipcheck", help="check whether already-measured keys clipped"
    )
    add_audio(p)
    p.add_argument("--keys", default=KEYS, help="e.g. 51-70")
    p.add_argument("--raw", type=Path, default=RAW_FILE)
    p.set_defaults(run=clipcheck)

    p = commands.add_parser("measure", help="sweep duty levels and record loudness")
    add_audio(p)
    p.add_argument("--keys", default=KEYS, help="e.g. 0-87, or 30-40,50")
    p.add_argument("--start", type=int, default=START_DUTY, help="lowest duty to try")
    p.add_argument("--step", type=int, default=DUTY_STEP)
    p.add_argument("--fine-step", type=int, default=FINE_STEP)
    p.add_argument("--fine-range", type=int, default=FINE_RANGE)
    p.add_argument(
        "--low-levels",
        type=int,
        default=LOW_LEVELS,
        help="reliable levels that get 5 strikes",
    )
    p.add_argument(
        "--spread-db",
        type=float,
        default=SPREAD_DB,
        help="spread that triggers 2 extra strikes",
    )
    p.add_argument(
        "--flat-db",
        type=float,
        default=FLAT_DB,
        help="gain per step treated as saturated",
    )
    p.add_argument(
        "--tail-ms", type=int, default=TAIL_MS, help="recorded after the release"
    )
    p.add_argument(
        "--quiet-db",
        type=float,
        default=QUIET_DB,
        help="how close to the room background before striking",
    )
    p.add_argument(
        "--max-quiet-wait",
        type=float,
        default=MAX_QUIET_WAIT_S,
        help="longest wait for quiet, in seconds",
    )
    p.add_argument(
        "--redo",
        action="store_true",
        help="measure keys again even if they're already in the raw file, replacing their results",
    )
    p.add_argument("--raw", type=Path, default=RAW_FILE)
    p.set_defaults(run=measure)

    def add_mapping(p):
        p.add_argument("--raw", type=Path, default=RAW_FILE)
        p.add_argument(
            "--flat-db",
            type=float,
            default=FLAT_DB,
            help="gain per step treated as saturated",
        )
        p.add_argument(
            "--min-margin",
            type=int,
            default=MIN_MARGIN,
            help="added to the lowest reliable duty",
        )
        p.add_argument(
            "--step",
            type=int,
            default=DUTY_STEP,
            help="span over which saturation is judged",
        )
        p.add_argument(
            "--cut-short-db", type=float, default=CUT_SHORT_DB, help="see CUT_SHORT_DB"
        )
        p.add_argument(
            "--neighbour-keys",
            type=int,
            default=NEIGHBOUR_KEYS,
            help="see NEIGHBOUR_KEYS",
        )
        p.add_argument("--smoothing", type=float, default=SMOOTHING, help="see SMOOTHING (0-1)")
        p.add_argument("--tilt-db", type=float, default=TILT_DB, help="see TILT_DB")

    p = commands.add_parser(
        "table", help="build the velocity table the controller plays from"
    )
    add_mapping(p)
    p.add_argument("--out", type=Path, default=TABLE_FILE)
    p.set_defaults(run=table)

    p = commands.add_parser(
        "report", help="write calibration/report.html, a page of charts, and open it"
    )
    add_mapping(p)
    p.add_argument("--out", type=Path, default=REPORT_FILE)
    p.add_argument(
        "--no-open",
        dest="open",
        action="store_false",
        help="write the page without opening it",
    )
    p.set_defaults(run=report)

    args = parser.parse_args()
    if not 0 <= getattr(args, "smoothing", 0) <= 1:
        sys.exit("SMOOTHING must be between 0 and 1")
    args.run(args)


if __name__ == "__main__":
    main()
