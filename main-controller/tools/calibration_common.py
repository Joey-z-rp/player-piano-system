#!/usr/bin/env python3
"""Shared by the calibration tools (calibrate_dynamics.py, compare_drives.py,
listen.py): the connection to the controller, the microphone, and measuring
one strike. The settings below are the hardware setup, so they live here once
for every tool; each tool's command line can still override them.
"""

from __future__ import annotations

import json
import statistics
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
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
MIC_GAIN_DB = 0.0

# Each strike
# How long each key is held down: long enough that the softest notes, which
# land ~200 ms after the press, are still held DECAY_AT_MS after they start,
# so a key that damps its own note shows up in the decay.
HOLD_MS = 500
TAIL_MS = 500  # recorded after the release, so the whole attack is captured

# Before each strike, wait until the previous note has died away: within
# QUIET_DB of the room's background, measured at the start of each run.
QUIET_DB = 6
MAX_QUIET_WAIT_S = 10  # strike anyway after this long, with a warning

# Hit detection: lower MARGIN_DB if real notes are logged "too short" or
# "silent"; raise it if clicks are logged as hits.
MARGIN_DB = 10  # how far above the background noise a note must be

# Restarting the drivers (RESET:ALL) pauses the controller's command queue for
# ~100 ms; wait this long before striking again, so the next press isn't late.
DRIVER_RESET_WAIT_S = 0.5

# Landing: the mechanism moving can be nearly as loud as a soft note (on some
# bass keys, for ~25-90 ms after the press), so the landing is found from the
# note backwards: its loudest LANDING_ANCHOR_MS, then back to where that sound
# began, the first 1 ms within LANDS_BELOW_PEAK_DB of it (and MARGIN_DB above
# the noise). The walk back crosses short dips (bass notes beat) but stops at
# a quiet gap of LANDING_GAP_MS, which separates the note from the mechanism.
LANDS_BELOW_PEAK_DB = 20
LANDING_ANCHOR_MS = 20
LANDING_GAP_MS = 10
# No hammer lands sooner than this (~50 ms at full duty) or later than
# LANDING_LATEST_MS (~200 ms at the softest). A sound without a landing in
# between is a thump (the key moving without the hammer striking the string),
# not a note: thumps sound alike at every duty and have no landing.
LANDING_EARLIEST_MS = 40
LANDING_LATEST_MS = 300
# Bounce: within BOUNCE_WINDOW_MS of the note starting, a jump of BOUNCE_DB
# within BOUNCE_RISE_MS that comes back to within BOUNCE_DB of the first peak
# counts as the hammer striking twice. (Treble notes swell and fade by 10-20
# dB as their strings beat, but never come back up that far.)
BOUNCE_DB = 6
BOUNCE_RISE_MS = 15
BOUNCE_WINDOW_MS = 200
DECAY_AT_MS = 200  # damping: how far the note has died away this long after it starts

# ============================================================================

PROJECT_DIR = Path(__file__).resolve().parent.parent
TABLE_FILE = (
    PROJECT_DIR / "data" / "velocity_table.json"
)  # uploaded with `pio run -t uploadfs`
PIANO_KEYS = 88
WINDOW_MS = 10  # level is the loudest window of this length
ATTACK_MS = 150  # ...within this long after the onset
SUSTAIN_MS = (
    100  # a note still sounds this long after the onset; a solenoid click doesn't
)
PRE_ROLL_MS = 150  # silence recorded before each strike, for the noise floor
# A note starts at the first sound that stays MARGIN_DB above the noise for
# this long. The mechanism moving can be that loud before a soft note (on some
# keys for ~50 ms), but not steadily.
ONSET_STEADY_MS = 30
CLIP_DBFS = (
    -1.0
)  # a strike whose raw peak is above this may have clipped the microphone
# Extra silence before a timed press, on top of PRE_ROLL_MS, so the audio
# stream has surely started by the time the press goes out.
LEAD_IN_MS = 100
LANDING_STEP_MS = 1  # resolution of landing times
LANDING_SEARCH_MS = 450  # how long after the press a landing is looked for


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


def note_onset(levels: np.ndarray, threshold_db: float) -> int | None:
    """Index of the WINDOW_MS window where the note starts in `levels`, or None."""
    steady = ONSET_STEADY_MS // WINDOW_MS
    above = levels > threshold_db
    for i in np.nonzero(above)[0]:
        if above[i : i + steady].all() and i + steady <= len(levels):
            return int(i)
    return None


def lasting_onset(levels: np.ndarray, threshold_db: float) -> int | None:
    """Like note_onset, but skipping sounds that don't last SUSTAIN_MS: the
    start of the first that does, else of the first sound at all."""
    first = start = note_onset(levels, threshold_db)
    while start is not None:
        later = levels[start + SUSTAIN_MS // WINDOW_MS :][:3]
        if len(later) and float(np.median(later)) > threshold_db:
            return start
        end = start
        while end < len(levels) and levels[end] > threshold_db:
            end += 1  # past this sound
        found = note_onset(levels[end:], threshold_db)
        start = None if found is None else end + found
    return first


def measure_strike(
    audio: np.ndarray, sample_rate: int, sos: np.ndarray, margin_db: float, later_onset: bool = False
):
    """Returns (result, peak_db, noise_db) for a recording that starts with PRE_ROLL_MS of
    silence. result is "hit", "silent", or "too short" (a click that didn't keep sounding).

    With later_onset, a first sound that doesn't keep sounding is taken as the
    mechanism, and the note is looked for after it. With the pedal down the
    press can be noisy for long enough to pass as a note's start; for the
    pedal-down measurements only, so the pedal-up ones are judged as before."""
    weighted = signal.sosfilt(sos, audio)
    levels = window_levels_db(weighted, sample_rate)
    pre = int(PRE_ROLL_MS / WINDOW_MS)
    # Skip the first window: the filter is still settling there.
    noise_db = float(np.median(levels[1:pre]))
    after = levels[pre:]
    if not (after > noise_db + margin_db).any():
        return "silent", float(after.max()), noise_db
    onset = note_onset(after, noise_db + margin_db)
    if later_onset:
        onset = lasting_onset(after, noise_db + margin_db)
    size = int(sample_rate * WINDOW_MS / 1000)
    if onset is None:  # only brief sounds: the mechanism, or a note cut off at once
        return "too short", sliding_peak_db(weighted[pre * size :], sample_rate), noise_db
    start = (pre + onset - 1) * size  # from one window early, so the attack isn't cut
    peak_db = sliding_peak_db(
        weighted[start : start + int(sample_rate * (ATTACK_MS + WINDOW_MS) / 1000)],
        sample_rate,
    )
    later = after[onset + int(SUSTAIN_MS / WINDOW_MS) :][:3]
    sustained = len(later) > 0 and float(np.median(later)) > noise_db + margin_db
    return ("hit" if sustained else "too short"), peak_db, noise_db


AUDIO_RETRIES = 5  # times to try again when the Mac's audio won't start a recording


def start_audio(sd, start):
    """Runs start() (which opens and starts a recording), trying again when the
    Mac's audio system refuses for a moment (e.g. CoreAudio error -308, after
    the screen locks or the microphone drops out briefly): it's restarted, and
    each try waits a little longer. Only the start is retried, before anything
    is sent to the piano."""
    for attempt in range(AUDIO_RETRIES + 1):
        try:
            return start()
        except sd.PortAudioError as error:
            if attempt == AUDIO_RETRIES:
                raise
            print(f"  the audio wouldn't start ({error}); restarting it and trying again", flush=True)
            sd._terminate()
            time.sleep(1 + attempt)
            sd._initialize()


class Recorder:
    def __init__(self, device, sample_rate: int):
        import sounddevice  # only needed when recording

        self.sd = sounddevice
        self.device = device
        self.sample_rate = sample_rate

    def record(self, seconds: float, during) -> np.ndarray:
        """Records for `seconds`, calling during() once recording has started."""
        frames = int(seconds * self.sample_rate)
        audio = start_audio(self.sd, lambda: self.sd.rec(
            frames,
            samplerate=self.sample_rate,
            channels=1,
            dtype="float32",
            device=self.device,
        ))
        during()
        self.sd.wait()
        return audio[:, 0]


class TimedRecorder:
    """Records like Recorder, but also works out which sample was being
    recorded at a given time.monotonic(), so the moment a command was sent can
    be found in the audio."""

    def __init__(self, device, sample_rate: int):
        import sounddevice  # only needed when recording

        self.sd = sounddevice
        self.device = device
        self.sample_rate = sample_rate

    def record(self, seconds: float, during):
        """Records for `seconds`, calling during() once recording has started.
        Returns the audio and a function from a time.monotonic() to a sample index."""
        frames = int(seconds * self.sample_rate)
        blocks: list[np.ndarray] = []
        stamps: list[tuple[int, int, float, float, float]] = []  # first sample, count, ADC time, stream time, monotonic
        recorded = [0]

        def callback(indata, count, times, _status):
            stamps.append((recorded[0], count, times.inputBufferAdcTime, times.currentTime, time.monotonic()))
            blocks.append(indata[:, 0].copy())
            recorded[0] += count

        def open_stream():
            stream = self.sd.InputStream(samplerate=self.sample_rate, channels=1, dtype="float32",
                                         device=self.device, callback=callback)
            stream.start()
            return stream

        stream = start_audio(self.sd, open_stream)  # started already, so not used with `with` (that starts it again)
        try:
            during()
            while recorded[0] < frames:
                time.sleep(0.01)
            latency = stream.latency
        finally:
            stream.stop()
            stream.close()

        # The stream clock and time.monotonic() differ by a fixed offset. Each
        # callback reads monotonic a little after the stream time, so the
        # smallest difference is the closest to the real offset.
        offset = min(mono - now for *_, now, mono in stamps)
        # When each block's first sample was recorded, on the stream clock.
        # Some audio drivers don't report it; then estimate it from the latency.
        if all(adc == 0 for _, _, adc, _, _ in stamps):
            starts = [(first, now - latency - count / self.sample_rate) for first, count, _, now, _ in stamps]
        else:
            starts = [(first, adc) for first, _, adc, _, _ in stamps]
        zero = statistics.median(adc - first / self.sample_rate for first, adc in starts)

        def sample_at(mono: float) -> int:
            return round((mono - offset - zero) * self.sample_rate)

        return np.concatenate(blocks)[:frames], sample_at


def record_press(recorder: TimedRecorder, piano: "Connection", press: str, release: str, hold_ms: int):
    """Records one press. Returns (audio starting PRE_ROLL_MS before the press
    went out, as measure_strike expects; how long sending it took in s), or
    None if it didn't go out cleanly and should be done again."""
    rate = recorder.sample_rate
    seconds = (LEAD_IN_MS + PRE_ROLL_MS + hold_ms + TAIL_MS) / 1000
    clean = []

    def during():
        time.sleep(LEAD_IN_MS / 1000)
        clean.append(piano.play(press, release, hold_ms))

    audio, sample_at = recorder.record(seconds, during)
    if not clean[0]:
        return None
    sent, send_s = piano.press_sent
    at = sample_at(sent)
    pre = int(rate * PRE_ROLL_MS / 1000)
    if not pre <= at < len(audio) - rate // 2:
        sys.exit(f"the press landed at sample {at} of {len(audio)}; the timing doesn't add up")
    return audio[at - pre:], send_s


def levels_1ms(audio: np.ndarray, sample_rate: int) -> np.ndarray:
    size = int(sample_rate * LANDING_STEP_MS / 1000)
    count = len(audio) // size
    rms = np.sqrt(np.mean(audio[: count * size].reshape(count, size) ** 2, axis=1))
    return 20 * np.log10(np.maximum(rms, 1e-10))


def smoothed_db(levels: np.ndarray, ms: int) -> np.ndarray:
    """levels averaged (as power) over a centred ms-long window."""
    count = max(ms // LANDING_STEP_MS, 1)
    power = np.convolve(10 ** (levels / 10), np.ones(count) / count, mode="same")
    return 10 * np.log10(np.maximum(power, 1e-20))


def lands_at(levels: np.ndarray, noise_db: float, margin_db: float) -> int | None:
    """levels: A-weighted LANDING_STEP_MS levels from the press. Returns ms from
    the press to the hammer landing, or None."""
    search = levels[: LANDING_SEARCH_MS // LANDING_STEP_MS]
    earliest = LANDING_EARLIEST_MS // LANDING_STEP_MS
    if len(search) <= earliest:
        return None
    anchor_levels, walk_levels = smoothed_db(search, LANDING_ANCHOR_MS), smoothed_db(search, 5)
    anchor = earliest + int(np.argmax(anchor_levels[earliest:]))
    threshold = max(noise_db + margin_db, float(anchor_levels[anchor]) - LANDS_BELOW_PEAK_DB)
    start, below, i = anchor, 0, anchor
    while i > earliest:
        i -= 1
        if walk_levels[i] >= threshold:
            start, below = i, 0
        else:
            below += 1
            if below >= LANDING_GAP_MS // LANDING_STEP_MS:
                break
    # The smoothing blurs the start by a few ms: take the first 1 ms level there.
    for i in range(max(start - 3, earliest), anchor + 1):
        if search[i] >= threshold:
            return i * LANDING_STEP_MS
    return None


def note_period_ms(key: int) -> float:
    """One cycle of a key's note (key 0 is A0, 27.5 Hz)."""
    return 1000 / (27.5 * 2 ** (key / 12))


def bounce_and_decay(audio: np.ndarray, sample_rate: int, sos: np.ndarray, noise_db: float,
                     margin_db: float, key: int | None = None) -> tuple[bool, float]:
    """For a recording that starts with PRE_ROLL_MS of quiet: whether the note
    has a second attack (the hammer striking twice), and how many dB it has
    died away DECAY_AT_MS after it starts. Given the key, the bounce check
    smooths over at least one cycle of its note: in WINDOW_MS slices a bass
    note's level swings by several dB every cycle, which looks like a bounce."""
    weighted = signal.sosfilt(sos, audio)
    levels = window_levels_db(weighted, sample_rate)
    after = levels[PRE_ROLL_MS // WINDOW_MS :]
    onset = note_onset(after, noise_db + margin_db)
    if onset is None:
        return False, float("nan")
    window = after[onset : onset + BOUNCE_WINDOW_MS // WINDOW_MS]
    peak_at = int(np.argmax(window[: ATTACK_MS // WINDOW_MS]))
    at = onset + DECAY_AT_MS // WINDOW_MS
    later = after[at : at + 3]
    decay = float(window[peak_at] - np.median(later)) if len(later) else float("nan")

    # Bounce, in LANDING_STEP_MS steps smoothed over the note's cycle.
    smooth_ms = WINDOW_MS if key is None else max(WINDOW_MS, int(np.ceil(1.2 * note_period_ms(key))))
    fine = smoothed_db(levels_1ms(weighted, sample_rate)[PRE_ROLL_MS // LANDING_STEP_MS :], smooth_ms)
    start = onset * WINDOW_MS // LANDING_STEP_MS
    span = fine[start : start + BOUNCE_WINDOW_MS // LANDING_STEP_MS]
    peak = int(np.argmax(span[: ATTACK_MS // LANDING_STEP_MS]))
    rise = max(BOUNCE_RISE_MS, smooth_ms + 10) // LANDING_STEP_MS
    bounce = False
    for i in range(peak + 2, len(span)):
        jumped = span[i] - span[max(peak + 1, i - rise) : i].min() >= BOUNCE_DB
        if jumped and span[i] >= span[peak] - BOUNCE_DB:
            bounce = True
            break
    return bounce, decay


@dataclass
class Strike:
    """Everything measured from one recorded press."""

    result: str  # "hit", "silent", "too short" or "thump" (see LANDING_LATEST_MS)
    peak_db: float
    noise_db: float
    sample_peak_db: float  # raw peak, for spotting clipping
    lands_ms: int | None  # hits only
    bounce: bool  # hits only
    decay_db: float  # hits only, else nan


def is_note(lands_ms: int | None) -> bool:
    """Whether a sound that kept going is a note rather than a thump."""
    return lands_ms is not None and LANDING_EARLIEST_MS <= lands_ms <= LANDING_LATEST_MS


def analyse_strike(audio: np.ndarray, sample_rate: int, sos: np.ndarray, margin_db: float,
                   key: int | None = None, later_onset: bool = False) -> Strike:
    """audio as record_press returns it; the key, if known, sets how the bounce
    check smooths (see bounce_and_decay). later_onset: see measure_strike."""
    result, peak, noise = measure_strike(audio, sample_rate, sos, margin_db, later_onset)
    sample_peak = 20 * np.log10(max(float(np.abs(audio).max()), 1e-10))
    if result != "hit":
        return Strike(result, peak, noise, sample_peak, None, False, float("nan"))
    pre = int(sample_rate * PRE_ROLL_MS / 1000)
    lands = lands_at(levels_1ms(signal.sosfilt(sos, audio)[pre:], sample_rate), noise, margin_db)
    bounce, decay = bounce_and_decay(audio, sample_rate, sos, noise, margin_db, key)
    if not is_note(lands):
        return Strike("thump", peak, noise, sample_peak, lands, False, decay)
    return Strike(result, peak, noise, sample_peak, lands, bounce, decay)


# --- controller --------------------------------------------------------------


class Connection:
    """How driver commands reach the controller."""

    def send(self, command: str) -> bool:
        """Sends a command. Returns True if it went through first time and promptly."""
        raise NotImplementedError

    def release_everything(self) -> None:
        raise NotImplementedError

    def config(self) -> dict:
        """The controller's play config (include/app/piano/play_config.h): the
        timing rules it plays pieces with, set on its config page."""
        if not hasattr(self, "_config"):
            self._config = self._read_config()
        return self._config

    def _read_config(self) -> dict:
        raise NotImplementedError

    def reset_drivers(self) -> None:
        """Restarts every driver board, then waits DRIVER_RESET_WAIT_S."""
        self._reset_drivers()
        time.sleep(DRIVER_RESET_WAIT_S)

    def _reset_drivers(self) -> None:
        raise NotImplementedError

    def strike(self, key: int, duty: float, hold_ms: int) -> bool:
        """Presses and releases a key. Returns False if the press was retried
        or late, in which case the strike can't be trusted and should be redone."""
        return self.play(f"P:{key}:{format_duty(duty)}", f"R:{key}:0", hold_ms)

    def play(self, press: str, release: str, hold_ms: int) -> bool:
        """Sends any press command, then the release hold_ms later. Same result
        as strike()."""
        time.sleep(PRE_ROLL_MS / 1000)
        sending = time.monotonic()
        pressed_cleanly = self.send(press)
        # When the press went out (time.monotonic()) and how long sending it took.
        self.press_sent = (sending, time.monotonic() - sending)
        time.sleep(hold_ms / 1000)
        released_cleanly = self.send(release)
        if not (pressed_cleanly and released_cleanly):
            self.release_everything()  # in case anything was left held during the outage
        return pressed_cleanly


    def play_sequence(self, commands: list[tuple[float, str]]) -> list[float]:
        """After PRE_ROLL_MS of silence, sends each (ms after the first,
        command) as close to on time as the link allows. Returns when each
        actually went out (time.monotonic()), so the real timing can be used."""
        time.sleep(PRE_ROLL_MS / 1000)
        start = time.monotonic()
        sent = []
        for at_ms, command in commands:
            due = start + at_ms / 1000
            while due - time.monotonic() > 0.003:
                time.sleep(due - time.monotonic() - 0.002)
            while time.monotonic() < due:
                pass
            sent.append(time.monotonic())
            self.send(command)
        return sent


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

    def _reset_drivers(self) -> None:
        self._post_until_sent("/api/driver/reset")

    def _read_config(self) -> dict:
        try:
            with urllib.request.urlopen(self.base + "/api/config", timeout=5) as response:
                return json.loads(response.read())["config"]
        except (OSError, urllib.error.URLError) as error:
            sys.exit(f"Couldn't read the controller's config ({error}). Is its firmware up to date?")


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

    def _command(self, text: str) -> str | None:
        """Sends a command and waits for its #OK. Returns the last other line
        starting with "#" before it (a reply such as #CONFIG), if any."""
        for _ in range(50):
            reply = None
            self.serial.reset_input_buffer()  # drop log output from earlier
            self.serial.write((text + "\n").encode())
            deadline = time.monotonic() + self.REPLY_TIMEOUT_S
            while time.monotonic() < deadline:
                line = self.serial.readline().decode(errors="replace").strip()
                if line == "#OK":
                    return reply
                if line.startswith("#ERR"):
                    reason = line[5:]
                    if reason == "queue full":
                        break  # the controller is catching up; try again
                    sys.exit(f"{text}: controller replied {reason}")
                if line.startswith("#"):
                    reply = line
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

    def _reset_drivers(self) -> None:
        self.send("RESET")

    def _read_config(self) -> dict:
        reply = self._command("CONFIG")
        if not reply or not reply.startswith("#CONFIG "):
            sys.exit("The controller didn't send its config. Is its firmware up to date?")
        return json.loads(reply[len("#CONFIG "):])


def connect(args) -> Connection:
    if args.connection == "usb":
        return UsbConnection(args.port)
    if not args.host:
        sys.exit(
            "Set HOST in the SETTINGS block of calibration_common.py, or pass --host"
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


# --- helpers -----------------------------------------------------------------


# How the controller drives a key press when its velocity table has landing
# times, and what it falls back to when the table doesn't say
# (DriveSettings in include/app/piano/velocity_table.h). calibrate_dynamics.py's
# settings go into each table's "drive".
# The sustain pedal, pressed and released with the commands in the
# controller's config (SUSTAIN_PEDAL_CHANNELS in include/app/piano/piano_controller.h).
PEDAL_CHANNELS = (88, 89)
PEDAL_SETTLE_S = 0.6  # after pressing it: down, and its thump died away, before a strike is recorded
PEDAL_LIFT_S = 0.3  # after releasing it


def set_pedal(piano: "Connection", down: bool) -> None:
    """Presses or releases the sustain pedal, then waits for it to settle."""
    command = piano.config()["pedal_press" if down else "pedal_release"]
    for channel in PEDAL_CHANNELS:
        piano.send(f"P:{channel}:{command}")
    time.sleep(PEDAL_SETTLE_S if down else PEDAL_LIFT_S)


DEFAULT_DRIVE = {"strike_ms": 50, "coast_duty": 20, "coast_offset_ms": 30, "clamp_offset_ms": -15, "clamp_ms": 0,
                 "clamp_duty": 60, "hold_duty": 25}
# A key's own settings in its table entry, and the "drive" setting each replaces.
KEY_DRIVE_FIELDS = {"coast": "coast_duty", "coast_offset_ms": "coast_offset_ms", "clamp_offset_ms": "clamp_offset_ms",
                    "clamp_ms": "clamp_ms", "clamp_duty": "clamp_duty", "hold": "hold_duty"}


def by_duty(value, duty: float):
    """A setting that's one value, or steps of [[strike duty above which,
    value], ...]: its value for a strike at this duty."""
    if not isinstance(value, list):
        return value
    return [v for above, v in value if duty > above or above == 0][-1]


def drive_command(key: int, duty: float, landing_ms: float | None, drive: dict, entry: dict | None = None,
                  repeat_clamp: tuple | None = None) -> str:
    """The press the controller sends (VelocityTable::pressCommand). The strike
    duty for strike_ms; then the coast duty, carrying the hammer through its
    flight; then:
      - without a clamp (clamp_ms 0): the hold, from coast_offset_ms after the
        hammer lands (negative = before);
      - with one: the clamp duty for clamp_ms from clamp_offset_ms after the
        hammer lands, pinning the key as it bottoms out so it can't rebound
        into a second strike, then the hold.
    Nothing starts before the strike ends. `entry` is the key's table entry,
    which can set its own of each (KEY_DRIVE_FIELDS); all but "coast" can be
    steps by strike duty (see by_duty). A repeat (struck again soon after the
    last on its key) is clamped with repeat_clamp (offset, ms, duty: see
    repeat_clamp() for the controller's) instead, unless its ms is 0. Without a
    landing time, a plain press."""
    entry = entry or {}
    if not landing_ms:
        return f"P:{key}:{format_duty(duty)}"

    def setting(name: str):
        own = next(k for k, v in KEY_DRIVE_FIELDS.items() if v == name)
        return by_duty(entry.get(own, drive[name]), duty)

    strike_ms = drive["strike_ms"]
    coast = format_duty(setting("coast_duty"))
    if repeat_clamp and repeat_clamp[1] > 0:
        offset, clamp_ms, clamp_duty = repeat_clamp
    else:
        offset, clamp_ms, clamp_duty = setting("clamp_offset_ms"), setting("clamp_ms"), setting("clamp_duty")
    clamp_ms = round(clamp_ms)
    if clamp_ms > 0:
        clamp_at = max(round(landing_ms + offset), strike_ms)
        phases = f"{coast}:{clamp_at - strike_ms}:{format_duty(clamp_duty)}:{clamp_ms}"
    else:
        hold_at = max(round(landing_ms + setting("coast_offset_ms")), strike_ms)
        phases = f"{coast}:{hold_at - strike_ms}"
    return f"P:{key}:{format_duty(duty)}:{strike_ms}:{phases}:{format_duty(setting('hold_duty'))}"


def repeat_clamp(config: dict) -> tuple:
    """The controller's repeat clamp (offset, ms, duty), from its config."""
    return (config["repeat_clamp_offset_ms"], config["repeat_clamp_ms"], config["repeat_clamp_duty"])


def format_duty(duty: float) -> str:
    """As the driver takes it: percent with up to two decimals, e.g. 32, 32.5, 32.25."""
    return f"{duty:.2f}".rstrip("0").rstrip(".")


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


def parse_keys(text: str) -> list[int]:
    keys: list[int] = []
    for part in text.split(","):
        first, _, last = part.partition("-")
        keys.extend(range(int(first), int(last or first) + 1))
    if not all(0 <= key < PIANO_KEYS for key in keys):
        sys.exit("keys must be 0-87")
    return keys


def devices(_args) -> None:
    import sounddevice

    print(sounddevice.query_devices())


# --- the tests' shared parts ---------------------------------------------------


class Session:
    """A connection to the controller, the microphone (to wait for quiet and to
    record strikes), and the room's background level."""

    def __init__(self, args, piano: "Connection | None" = None):
        self.args = args
        self.piano = piano or connect(args)
        self.recorder = Recorder(args.device, args.sample_rate)  # for waiting until quiet
        self.timed = TimedRecorder(args.device, args.sample_rate)  # for the strikes
        self.sos = a_weighting(args.sample_rate)
        self.room_db = measure_room(self.recorder, self.sos)


def add_session_args(p) -> None:
    """The command-line options a Session needs, with the settings above as defaults."""
    p.add_argument("--connection", choices=["usb", "wifi"], default=CONNECTION)
    p.add_argument("--port", default=SERIAL_PORT)
    p.add_argument("--host", default=HOST)
    p.add_argument("--device", default=DEVICE, type=lambda v: int(v) if v.isdigit() else v)
    p.add_argument("--sample-rate", type=int, default=SAMPLE_RATE)
    p.add_argument("--margin-db", type=float, default=MARGIN_DB)
    p.add_argument("--gain-db", type=float, default=MIC_GAIN_DB)


def table_command(key: int, velocity: int, table: dict, pedal: bool = False) -> str:
    """The press the controller sends for this key and velocity, from a
    velocity table (its JSON); with the pedal down, from the key's pedal entry
    if it has one."""
    entry = table["keys"][str(key)]
    if pedal and "pedal" in entry:
        entry = entry["pedal"]
    landing = entry.get("landing_ms") or table.get("landing_ms")
    drive = {**DEFAULT_DRIVE, **table.get("drive", {})}
    duty = entry["duty"][velocity - 1]
    return drive_command(key, duty, landing[velocity - 1] if landing else None, drive, entry)


def table_landing(key: int, velocity: int, table: dict, pedal: bool = False) -> int:
    """When the key's hammer lands at this velocity (ms after the press), from a velocity table."""
    entry = table["keys"][str(key)]
    if pedal and "pedal" in entry:
        entry = entry["pedal"]
    landing = entry.get("landing_ms") or table.get("landing_ms")
    if not landing:
        sys.exit("the velocity table has no landing times")
    return landing[velocity - 1]

