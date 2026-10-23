#!/usr/bin/env python3
"""Fits the solenoid heating tests (calibration/heat_tests.json) and prints
the coil settings for the heat protection (Settings -> Solenoid heat).

Each test: a coil held at a steady duty from cold, its surface read every 20 s,
then let cool. The surface follows rise(t) = final x (1 - e^(-t/tau)) while
heating and rise0 x e^(-t/tau) while cooling. The surface (or the frame) runs
cooler than the winding, which is what the protection must watch: the
resistance read hot gives the winding's average rise (copper +0.393%/C), and
the settings are scaled up to it. tau is taken a little faster than measured,
as the winding leads its surface: the protection then warns early, not late.

  python3 tools/heat_fit.py [--file calibration/heat_tests.json]
"""
import argparse
import json
from pathlib import Path

import numpy as np
from scipy.optimize import curve_fit

COPPER_PER_C = 0.00393
TAU_MARGIN = 0.9  # tau used: this times the heating fit's
TEST_FILE = Path(__file__).resolve().parent.parent / "calibration" / "heat_tests.json"


def readings(series: list, last: dict, room: float) -> tuple[np.ndarray, np.ndarray]:
    """Seconds from the start and the rise above the room, with the late reading."""
    t = list(np.arange(len(series)) * 20.0) + [(len(series) - 1) * 20.0 + last["after_s"]]
    rise = [c - room for c in series] + [last["c"] - room]
    return np.array(t), np.array(rise)


def fit(name: str, coil: dict, volts: float) -> None:
    room = coil["room_c"]
    t, rise = readings(coil["heating_every_20s_c"], coil["heating_last"], room)
    heating = lambda t, final, tau, lag: final * (1 - np.exp(-(t - lag) / tau)) * (t > lag)  # noqa: E731
    (final, tau, lag), _ = curve_fit(heating, t, rise, p0=[rise.max() * 1.2, 600, 20], maxfev=20000)
    rms = float(np.sqrt(np.mean((heating(t, final, tau, lag) - rise) ** 2)))
    tc, cooling_rise = readings(coil["cooling_every_20s_c"], coil["cooling_last"], room)
    (_, tau_cool), _ = curve_fit(lambda t, start, tau: start * np.exp(-t / tau), tc, cooling_rise,
                                 p0=[cooling_rise[0], 600])

    watts = (coil["duty"] / 100 * volts) ** 2 / coil["cold_ohms"]  # flyback diode: duty squared
    winding = (coil["hot_ohms"] / coil["cold_ohms"] - 1) / COPPER_PER_C
    surface_then = coil["hot_ohms_at_c"] - room
    hotter = winding / surface_then  # the winding over the surface read
    rth = final * hotter / watts
    print(f"{name} ({coil['duty']}% held, {watts:.1f} W; read on {coil['measured_on']})")
    print(f"  surface: heads for +{final:.1f} C, tau {tau:.0f} s heating / {tau_cool:.0f} s cooling (fit within {rms:.2f} C)")
    print(f"  winding: +{winding:.0f} C by its resistance when the surface was +{surface_then:.0f} C ({hotter:.2f}x)")
    print(f"  held there it'd settle about +{final * hotter:.0f} C ({room + final * hotter:.0f} C in this room)")
    print(f"  settings: resistance {coil['cold_ohms']} ohms, heating {rth:.1f} C/W, time constant {tau * TAU_MARGIN:.0f} s\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--file", type=Path, default=TEST_FILE)
    args = parser.parse_args()
    tests = json.loads(args.file.read_text())
    for name, coil in tests["coils"].items():
        fit(name, coil, tests["supply_volts"])


if __name__ == "__main__":
    main()
