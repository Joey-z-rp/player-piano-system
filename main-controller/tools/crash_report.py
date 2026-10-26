"""The controller's last crash, as code lines.

The controller records a crash (or a watchdog) in flash and shows a summary
at /api/system: which task, why, and where (program addresses), and which
firmware it was (its ELF's SHA-256). This finds that firmware among the
builds kept in firmware-builds/ (scripts/keep_build.py) and the current
build, and turns the addresses into functions and lines.

  python tools/crash_report.py               # from piano.local
  python tools/crash_report.py 192.168.0.42  # or its address
  python tools/crash_report.py --save crash.bin   # also download the core dump
"""

from __future__ import annotations

import argparse
import glob
import gzip
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ADDR2LINE = os.path.expanduser(
    "~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line"
)


def find_build(sha: str) -> str | None:
    """The ELF whose SHA-256 starts with sha: a kept build, or a current one."""
    for path in glob.glob(os.path.join(PROJECT, "firmware-builds", f"*_{sha}.elf.gz")):
        return path
    for path in glob.glob(os.path.join(PROJECT, ".pio", "build", "*", "firmware.elf")):
        with open(path, "rb") as f:
            if hashlib.sha256(f.read()).hexdigest().startswith(sha):
                return path
    return None


def lines(elf: str, addresses: list[str]) -> list[str]:
    if elf.endswith(".gz"):
        with tempfile.NamedTemporaryFile(suffix=".elf", delete=False) as out, gzip.open(elf) as f:
            shutil.copyfileobj(f, out)
            elf = out.name
    try:
        result = subprocess.run([ADDR2LINE, "-pfiaC", "-e", elf, *addresses], capture_output=True, text=True, check=True)
    finally:
        if elf.startswith(tempfile.gettempdir()):
            os.remove(elf)
    return [line.replace(PROJECT + "/", "") for line in result.stdout.splitlines()]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("host", nargs="?", default="piano.local")
    parser.add_argument("--save", metavar="FILE", help="also save the core dump (for espcoredump)")
    args = parser.parse_args()
    base = f"http://{args.host}"
    with urllib.request.urlopen(base + "/api/system", timeout=10) as response:
        system = json.load(response)
    restart = system.get("restart", {})
    print(f"Running {system['firmware']['version']}, built {system['firmware']['built']}")
    print(f"This start: {restart.get('reason', '?')}" +
          (f"; last unexpected restart: {restart['lastUnexpected']}" if restart.get("lastUnexpected") else ""))
    crash = system.get("crash")
    if not crash:
        print("No crash recorded.")
        return
    print(f"\nCrash in task {crash['task']}: {crash['cause'] or 'see the backtrace'} at {crash['pc']}" +
          (f" (address {crash['address']})" if crash["cause"] in ("LoadProhibited", "StoreProhibited") else ""))
    print(f"Firmware {crash['firmware']}{' (the one running now)' if crash['thisFirmware'] else ''}")
    if args.save:
        with urllib.request.urlopen(base + "/api/system/crash", timeout=30) as response, open(args.save, "wb") as f:
            f.write(response.read())
        print(f"Core dump saved to {args.save}")
    elf = find_build(crash["firmware"])
    if elf is None:
        print("\nThat build isn't in firmware-builds/ or .pio/build, so the addresses can't be turned into code:")
        print("  " + " ".join(crash["backtrace"]))
        sys.exit(1)
    print(f"Build: {os.path.relpath(elf, PROJECT)}\n")
    print("Backtrace (innermost first)" + (" - may be cut short (corrupted)" if crash["backtraceCorrupted"] else "") + ":")
    for line in lines(elf, crash["backtrace"] or [crash["pc"]]):
        print("  " + line)


if __name__ == "__main__":
    main()
