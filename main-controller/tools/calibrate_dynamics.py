#!/usr/bin/env python3
"""Measure how loud each key plays at each duty cycle.

Strikes keys through the controller's /api/driver/command endpoint, records
each strike with a microphone, and measures its A-weighted peak level. The
results feed a per-key velocity -> duty mapping.

Settings live in the SETTINGS block below (and the hardware setup in
calibration_common.py); any of them can be overridden
for one run on the command line (see --help on each command).

Steps:
  1. Plug the controller into this computer by USB (or set CONNECTION to
     "wifi" and set HOST), list input devices, pick a microphone without
     automatic gain control, and set DEVICE. The connection and microphone
     settings are in calibration_common.py, shared with the other tools:
       python3 tools/calibrate_dynamics.py devices
  2. Set the mic gain so the loudest key peaks around -6 dBFS:
       python3 tools/calibrate_dynamics.py level
  3. Measure (saves after each key; rerun the same command to resume):
       python3 tools/calibrate_dynamics.py measure
     To recalibrate some keys, replacing their earlier results:
       python3 tools/calibrate_dynamics.py measure --keys 39 --redo
  4. Build the velocity table the controller plays from (also prints each
     key's min duty, max duty and range, with any warnings), then upload it.
     It includes when the hammer lands at each velocity, if hold_tests.py
     flight has been run (see LANDING_FILE):
       python3 tools/calibrate_dynamics.py table
       pio run -t uploadfs
  5. See it all as charts in the browser (writes calibration/report.html):
       python3 tools/calibrate_dynamics.py report

With the sustain pedal down a key is lighter (the pedal has already lifted
the dampers it would lift), so it plays louder and lands sooner at the same
duty. To calibrate for that, measure with the pedal down, at the same mic
gain as the pedal-up measurements (it's saved apart, in PEDAL_RAW_FILE):
       python3 tools/calibrate_dynamics.py measure --pedal --keys 20-50
table then gives each key measured both ways a "pedal" entry: the duty that
plays each velocity as loud as it plays with the pedal up, and when the hammer
lands, which the controller uses for notes played with the pedal down. Its
clamps come from PEDAL_BOUNCE_CLAMP (review --pedal and report --pedal show
the pedal-down measurements).

Before each key the drivers are restarted. Where the key starts to play is
taken from the velocity table (an earlier calibration; see START_FROM_TABLE),
or else searched for live. It's then swept in SOFT_STEP steps from
BELOW_START under that to SOFT_RANGE above it, FINE_STEP steps to FINE_RANGE
above it, then DUTY_STEP steps. Every strike is recorded (RECORDING_DIR), and
table and report analyse the recordings, so the analysis can be improved
without measuring again. Each level gets 3 strikes, or 5
while the key hasn't yet played reliably, plus 2 more when the 3 disagree by
over SPREAD_DB. The sweep stops once two DUTY_STEP spans in a row add under
FLAT_DB. Strikes are driven as the controller plays them (see STRIKE_MS), and
each one records when the hammer landed, whether it bounced, and how fast the
note died away; the table lists keys that bounced or were damped.

The settings and command line are here; the work is in calibration_measure.py
(measure, level, clipcheck), calibration_analysis.py (table, report: each key's
summary and the velocity mapping), calibration_review.py (review) and
calibration_data.py (raw.csv, the recordings and flags).

Requires numpy and scipy; measuring also needs sounddevice, and pyserial
for a USB connection.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from types import SimpleNamespace

# The other calibration modules sit next to this file; make them findable however this is loaded.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from calibration_analysis import report, table  # noqa: E402
from calibration_common import (  # noqa: E402  (shared setup, audio and connection)
    CONNECTION,
    DEVICE,
    HOLD_MS,
    HOST,
    MARGIN_DB,
    MAX_QUIET_WAIT_S,
    MIC_GAIN_DB,
    PROJECT_DIR,
    QUIET_DB,
    SAMPLE_RATE,
    SERIAL_PORT,
    TABLE_FILE,
    TAIL_MS,
    devices,
)
from calibration_data import bounce_clamps  # noqa: E402
from calibration_measure import check, clipcheck, level, measure  # noqa: E402
from calibration_review import review  # noqa: E402

# === SETTINGS ===============================================================

# Sweep
KEYS = "0-87"  # e.g. "0-87", "39", or "30-40,50"
START_DUTY = 25  # lowest duty tried
DUTY_STEP = (
    5  # step for finding where the key starts to sound, and above the fine region
)
FINE_STEP = 1  # step near the threshold, where loudness changes fastest
FINE_RANGE = 15  # how far above the first sounding duty to keep using FINE_STEP
# Just above where the key starts to play, loudness changes fastest of all
# (~10 dB in the first 1%): SOFT_STEP steps for SOFT_RANGE from there. On keys
# 0-40 the real start was within 1% below to 2% above an earlier
# calibration's, so this covers at least 1% above the real start.
SOFT_STEP = 0.25
SOFT_RANGE = 3
# Where each key starts to play is taken from the velocity table's "min" (an
# earlier calibration), so nothing judged during the run decides what gets
# measured: SOFT_STEP steps from BELOW_START under it. The real start can be
# up to ~1% lower, but with MIN_MARGIN the table never plays that low, so 0
# skips it. Keys without a "min", or with --search, are searched for live
# instead (DUTY_STEP, then FINE_STEP).
START_FROM_TABLE = True
BELOW_START = 0
# A key's first press after resting plays up to ~6 dB softer and lands later
# (or not at all near its start); the next ones are normal. So each key gets
# this many unrecorded presses at WARM_UP_DUTY before it's measured.
WARM_UP_PRESSES = 1
WARM_UP_DUTY = 50
START_MAY_BE_ABOVE = 2
LOW_LEVELS = 3  # the first reliable levels get 5 strikes instead of 3
SPREAD_DB = 1.5  # 3 strikes further apart than this get 2 more
FLAT_DB = 0.5  # two DUTY_STEP spans in a row adding less than this = saturated

# How each press is driven. table writes these into the velocity table, and
# the controller and listen.py play from it (see drive_command in
# calibration_common.py); measuring uses them too. The strike duty for
# STRIKE_MS; then COAST_DUTY, carrying the hammer through its flight; then:
#   - without a clamp (CLAMP_MS 0): HOLD_DUTY from COAST_OFFSET_MS after the
#     hammer lands (negative = before) until the release;
#   - with one: CLAMP_DUTY for CLAMP_MS from CLAMP_OFFSET_MS after the hammer
#     lands, pinning the key as it bottoms out so it can't rebound into a
#     second strike, then HOLD_DUTY.
# Nothing starts before the strike ends. While searching for where a key
# starts to play, strikes coast at COAST_DUTY all the way to the release.
STRIKE_MS = 50
COAST_DUTY = 20
COAST_OFFSET_MS = 10
CLAMP_OFFSET_MS = -15
CLAMP_MS = 0  # no clamp
CLAMP_DUTY = 60
HOLD_DUTY = 25
# Per key, where a key needs something other than the above. All but
# KEY_COAST_DUTY can be one value, or steps of (strike duty above which,
# value), e.g. a clamp only on strikes above 40%: KEY_CLAMP_MS = {30: [(0, 0),
# (40, 40)]}. Set a group with a comprehension and override single keys after
# it (a later entry wins):
#   KEY_CLAMP_MS = {**{k: [(0, 0), (40, 40)] for k in BOUNCE_KEYS}, 30: [(0, 0), (35, 40)]}
# Whatever starts after the hammer has escaped doesn't change loudness; the
# coast pushes during the flight, so a key whose coast changes needs measuring
# again.
# Keys that bounce (double strikes, heard on the review page): a clamp on
# strikes above `above` (% duty), starting `offset` ms from the hammer landing
# (negative = before it), for `ms` at `duty` %. Other keys have no clamp.
# A key can have up to 5 sets, each from its `above` up to the next set's,
# e.g. 30: [(35, -15, 40, 60), (40, -20, 40, 60), (50, -25, 40, 65)].
#     key: (above, offset, ms, duty), or a list of them
BOUNCE_CLAMP: dict[int, tuple | list[tuple]] = {
    24: [(45, -20, 100, 100), (55, -30, 100, 100)],
    26: [(40, -10, 80, 100), (60, -10, 90, 100)],
    27: [(35, -10, 100, 100), (40, -15, 100, 100)],
    28: [(32, -10, 100, 100), (40, -15, 100, 100)],
    29: [(40, -15, 100, 100)],
    30: [(30, -5, 100, 100), (45, 0, 100, 100)],
    31: [(38, -15, 100, 100), (43, -20, 100, 100)],
    32: [(38, -15, 100, 100), (43, -20, 100, 100)],
    34: [(35, -10, 100, 100)],
    36: [(34, -10, 100, 100), (39, -15, 100, 100)],
    37: [(34, -10, 100, 100), (39, -15, 100, 100)],
    38: [(34, -10, 100, 100), (37, -15, 100, 100)],
    39: [(36, -10, 100, 100)],
    40: [(37, -10, 100, 100), (39, -15, 100, 100)],
    41: [(39, -10, 100, 100)],
    42: [(30, -10, 100, 100)],
    44: [(33, -10, 100, 100)],
    45: [(34, -10, 100, 100), (42, -15, 100, 100)],
    46: [(35, -15, 100, 100)],
    47: [(30, -10, 100, 100)],
    48: [(37, -10, 100, 100)],
    50: [(36, -10, 100, 100)],
    51: [(35, -10, 100, 100)],
    52: [(33, -10, 100, 100)],
    53: [(33, -10, 100, 100)],
    56: [(36, -10, 100, 100)],
    57: [(32, -10, 100, 100)],
    58: [(44, -10, 100, 100)],
    59: [(30, -10, 100, 100)],
    61: [(32, -10, 100, 100)],
    64: [(50, -10, 100, 100)],
}
BOUNCE_KEYS = list(BOUNCE_CLAMP)
KEY_CLAMP_MS, KEY_CLAMP_OFFSET_MS, KEY_CLAMP_DUTY = bounce_clamps(BOUNCE_CLAMP)
# With the pedal down, the clamps instead: the same form, with `above` a
# pedal-down strike duty and `offset` from the pedal-down landing. Keys not
# here have no clamp with the pedal down. table prints a draft for the keys in
# BOUNCE_CLAMP, from where their clamps start in velocity, to start from.
PEDAL_BOUNCE_CLAMP: dict[int, tuple | list[tuple]] = {
    26: [(37, -10, 80, 100)],
    27: [(33, -10, 100, 100), (43, -15, 100, 100)],
    28: [(28, -40, 100, 100), (29, -30, 100, 100), (33, -20, 100, 100)],
    29: [(31, -20, 100, 100)],
    30: [(30, -20, 100, 100), (33, -15, 100, 100)],
    31: [(34, -10, 100, 100), (40, -20, 100, 100)],
    32: [(34, -20, 100, 100), (42, -30, 100, 100), (46, -20, 100, 100)],
    33: [(34, -10, 100, 100)],
    34: [(32, -15, 100, 100)],
    35: [(40, -10, 100, 100)],
    36: [(31, -10, 100, 100)],
    37: [(32.5, -10, 100, 100), (40, -20, 100, 100)],
    38: [(31, -10, 100, 100), (41, -20, 100, 100)],
    39: [(32.5, -10, 100, 100)],
    40: [(35, -20, 100, 100)],
    41: [(36, -10, 100, 100)],
    42: [(28.5, -10, 100, 100)],
    44: [(30, -10, 100, 100)],
    45: [(34, -10, 100, 100), (39, -20, 100, 100)],
    46: [(32, -10, 100, 100)],
    47: [(30, -10, 100, 100)],
    48: [(34, -10, 100, 100)],
    50: [(32, -10, 100, 100)],
    51: [(30, -10, 100, 100)],
    52: [(31, -10, 100, 100)],
    53: [(31, -10, 100, 100)],
    56: [(31, -10, 100, 100)],
    57: [(30, -10, 100, 100)],
    58: [(38, -10, 100, 100)],
    59: [(28, -10, 100, 100)],
    61: [(33, -10, 100, 100)],  # heard > 34%
}
# The latest a key's hammer may land with the pedal down, in ms: a key whose
# softest pedal-down note lands later starts its velocities from the lowest
# duty that lands in time instead (its softest notes are a little louder). The
# latest landing sets how long after the MIDI everything plays, so "up" keeps
# it to the latest with the pedal up; None doesn't limit it.
PEDAL_MAX_LANDING_MS: int | str | None = "up"
PEDAL_KEY_CLAMP_MS, PEDAL_KEY_CLAMP_OFFSET_MS, PEDAL_KEY_CLAMP_DUTY = bounce_clamps(
    PEDAL_BOUNCE_CLAMP
)
KEY_HOLD_DUTY: dict[int, float | list[tuple[float, float]]] = {
    k: 30 for k in BOUNCE_KEYS
}
KEY_COAST_OFFSET_MS: dict[int, float | list[tuple[float, float]]] = {26: -40}
KEY_COAST_DUTY: dict[int, float] = {}
# When to expect the hammer to land, so the measurement doesn't depend on
# detecting it: (duty above where the key starts, latest landing in ms), the
# slowest seen on keys 0-40. Landing gets later the closer a duty is to where
# the key really starts, so to err on the late side the curve is read as if the
# key starts as high as it might: START_MAY_BE_ABOVE over the table's "min" (on
# keys 0-40 the real start was up to 2% above it), or at the first duty the
# live search heard.
LANDING_LIMIT = [
    (0, 206),
    (0.5, 175),
    (1, 159),
    (2, 148),
    (3, 136),
    (4.5, 121),
    (7, 110),
    (10.5, 99),
    (15, 91),
    (21, 82),
    (30, 72),
    (42, 66),
    (60, 60),
]

# Analysis
MIN_MARGIN = 1.0  # added to the lowest reliable duty, for safety
# A key's max duty, the top of its velocity range: the loudest it was measured
# (True), or where it stops getting louder: two DUTY_STEP spans in a row adding
# under FLAT_DB (False). The loudest is ~0.85 dB louder on most keys, though
# the last of it costs a lot of duty.
MAX_AT_LOUDEST = True
# A key whose notes die away (by DECAY_AT_MS in calibration_common) this many
# dB more than its neighbours' (NEIGHBOUR_KEYS either side) is flagged as
# damped: something may be stopping its string.
DAMPED_DB = 6
# Ignore each key's first strike: after resting, a key's first press plays
# softer and later (keys measured before the warm-up press was added).
IGNORE_FIRST_STRIKE = True
# A duty level needs this many strikes to count when finding where a key
# starts to play reliably (the live search strikes each duty once).
MIN_LEVEL_STRIKES = 3
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
SMOOTHING = 0.1
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
RAW_FILE = PROJECT_DIR / "calibration" / "raw.csv"
# When the hammer lands after the press: the controller coasts each strike
# until then and sends presses that much early, so soft and loud notes sound
# on time. The calibration measures it for every key it sweeps; until a key
# has been calibrated that way, the table falls back to `hold_tests.py flight`
# of a few test keys (this file). With neither, the table has no landing
# times and the controller plays as before.
LANDING_FILE = PROJECT_DIR / "calibration" / "hold_tests.json"
REPORT_FILE = PROJECT_DIR / "calibration" / "report.html"
# Every strike's recording, one file per key (~5-8 MB), so the analysis can be
# checked and redone without measuring again: table and report re-analyse
# every key that has recordings.
RECORDING_DIR = PROJECT_DIR / "calibration" / "recordings"
# Your verdicts from the review page (`review`): keys flagged by ear as
# damped or bouncing, with comments, and strikes overridden as a note or a
# thump. table and report apply them.
FLAGS_FILE = PROJECT_DIR / "calibration" / "flags.json"
# check: a rough look at the table as the controller plays it.
CHECK_VELOCITIES = [1, 16, 32, 48, 64, 80, 96, 108, 118, 127]  # closer together at the loud end
CHECK_STRIKES = 2  # per velocity
CHECK_FLAG_DB = 3  # flag a velocity this far from the key's typical difference
CHECK_FILE = PROJECT_DIR / "calibration" / "check.json"
CHECK_PEDAL_FILE = PROJECT_DIR / "calibration" / "check_pedal.json"
# The same three with the pedal down (measure --pedal, review --pedal).
PEDAL_RAW_FILE = PROJECT_DIR / "calibration" / "raw_pedal.csv"
PEDAL_RECORDING_DIR = PROJECT_DIR / "calibration" / "recordings_pedal"
PEDAL_FLAGS_FILE = PROJECT_DIR / "calibration" / "flags_pedal.json"

# ============================================================================


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

    def add_pedal(p, what):
        p.add_argument("--pedal", action="store_true", help=what)

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

    p = commands.add_parser(
        "check", help="play the table as the controller does at a few velocities, against the levels it expects"
    )
    add_audio(p)
    p.add_argument("--keys", default=KEYS, help="e.g. 0-87, or 30-40,50")
    p.add_argument("--velocities", type=lambda v: [int(x) for x in v.split(",")], default=CHECK_VELOCITIES,
                   help="e.g. 1,32,64,127")
    p.add_argument("--strikes", type=int, default=CHECK_STRIKES, help="per velocity")
    p.add_argument("--flag-db", type=float, default=CHECK_FLAG_DB, help="see CHECK_FLAG_DB")
    p.add_argument("--table", type=Path, default=TABLE_FILE)
    p.add_argument("--out", type=Path, default=None, help="results file (default CHECK_FILE, or CHECK_PEDAL_FILE)")
    add_pedal(p, "with the sustain pedal down, playing the pedal entries")
    p.set_defaults(run=check)

    p = commands.add_parser("measure", help="sweep duty levels and record loudness")
    add_audio(p)
    p.add_argument("--keys", default=KEYS, help="e.g. 0-87, or 30-40,50")
    p.add_argument("--start", type=int, default=START_DUTY, help="lowest duty to try")
    p.add_argument("--step", type=int, default=DUTY_STEP)
    p.add_argument("--fine-step", type=float, default=FINE_STEP)
    p.add_argument("--fine-range", type=float, default=FINE_RANGE)
    p.add_argument("--soft-step", type=float, default=SOFT_STEP)
    p.add_argument(
        "--below-start", type=float, default=BELOW_START, help="see BELOW_START"
    )
    p.add_argument(
        "--search",
        dest="from_table",
        action="store_false",
        default=START_FROM_TABLE,
        help="search live for where each key starts, instead of using the table's min",
    )
    p.add_argument(
        "--table",
        type=Path,
        default=TABLE_FILE,
        help="velocity table to take each key's start from",
    )
    p.add_argument("--soft-range", type=float, default=SOFT_RANGE)
    p.add_argument("--damped-db", type=float, default=DAMPED_DB, help="see DAMPED_DB")
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
    p.add_argument(
        "--recordings", type=Path, default=RECORDING_DIR, help="see RECORDING_DIR"
    )
    add_pedal(p, "with the sustain pedal down for every strike, into PEDAL_RAW_FILE")
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
            type=float,
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
        p.add_argument(
            "--smoothing", type=float, default=SMOOTHING, help="see SMOOTHING (0-1)"
        )
        p.add_argument("--tilt-db", type=float, default=TILT_DB, help="see TILT_DB")
        p.add_argument(
            "--damped-db", type=float, default=DAMPED_DB, help="see DAMPED_DB"
        )
        p.add_argument(
            "--recordings", type=Path, default=RECORDING_DIR, help="see RECORDING_DIR"
        )
        p.add_argument("--margin-db", type=float, default=MARGIN_DB)
        p.add_argument("--flags", type=Path, default=FLAGS_FILE, help="see FLAGS_FILE")
        p.add_argument(
            "--pedal-raw", type=Path, default=PEDAL_RAW_FILE, help="see PEDAL_RAW_FILE"
        )
        p.add_argument("--pedal-recordings", type=Path, default=PEDAL_RECORDING_DIR)

    p = commands.add_parser(
        "table", help="build the velocity table the controller plays from"
    )
    add_mapping(p)
    p.add_argument("--out", type=Path, default=TABLE_FILE)
    p.add_argument(
        "--landing", type=Path, default=LANDING_FILE, help="see LANDING_FILE"
    )
    p.add_argument(
        "--drive-only",
        action="store_true",
        help="only rewrite the drive settings (clamp, offsets, hold) in the existing table: no analysis",
    )
    p.set_defaults(run=table)

    p = commands.add_parser(
        "review",
        help="listen to strikes and flag them, on a page served from this computer",
    )
    add_mapping(p)
    p.add_argument("--port", type=int, default=8765)
    p.add_argument(
        "--no-open", dest="open", action="store_false", help="don't open the browser"
    )
    add_pedal(p, "the pedal-down measurements")
    p.set_defaults(run=review)

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
    add_pedal(p, "the pedal-down measurements")
    p.set_defaults(run=report)

    args = parser.parse_args()
    # The SETTINGS block, for the modules that do the work.
    args.cfg = SimpleNamespace(
        **{name: value for name, value in globals().items() if name.isupper()}
    )
    if not 0 <= getattr(args, "smoothing", 0) <= 1:
        sys.exit("SMOOTHING must be between 0 and 1")
    if getattr(args, "out", "unset") is None:  # check
        args.out = CHECK_PEDAL_FILE if getattr(args, "pedal", False) else CHECK_FILE
    if getattr(args, "pedal", False):
        # The pedal-down files, unless others were given.
        for name, usual, pedal in (
            ("raw", RAW_FILE, PEDAL_RAW_FILE),
            ("recordings", RECORDING_DIR, PEDAL_RECORDING_DIR),
            ("flags", FLAGS_FILE, PEDAL_FLAGS_FILE),
        ):
            if getattr(args, name, None) == usual:
                setattr(args, name, pedal)
    else:
        args.pedal = False
    args.run(args)


if __name__ == "__main__":
    main()
