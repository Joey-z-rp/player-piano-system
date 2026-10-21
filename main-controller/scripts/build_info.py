# Runs before each firmware build (extra_scripts in platformio.ini):
#  - stamps the firmware with what it was built from: include/firmware_version.h
#    gets the git commit ("+changes" if there were uncommitted edits) and the
#    build time, rewritten only when they change so little is rebuilt;
#  - gives the firmware the secrets in secrets.ini (gitignored; see
#    secrets.example.ini), if it's there: the Wi-Fi to fall back on and the
#    over-the-air update password.
import configparser
import datetime
import os
import subprocess

Import("env")  # noqa: F821  (SCons)

project = env.subst("$PROJECT_DIR")  # noqa: F821


def git(*args):
    try:
        return subprocess.check_output(["git", *args], cwd=project, stderr=subprocess.DEVNULL).decode().strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


commit = git("rev-parse", "--short", "HEAD") or "unknown"
changed = git("status", "--porcelain", "--", ".")
version = commit + ("+changes" if changed else "")
built = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
header = os.path.join(project, "include", "firmware_version.h")
text = (
    "// Written by scripts/build_info.py at each build: don't edit, not in git.\n"
    "#pragma once\n"
    f'#define FIRMWARE_VERSION "{version}"\n'
    f'#define FIRMWARE_BUILT "{built}"\n'
)
try:
    with open(header) as f:
        old = f.read()
except OSError:
    old = ""
if old != text:
    with open(header, "w") as f:
        f.write(text)
print(f"Firmware {version}, built {built}")

secrets = configparser.ConfigParser()
secrets.read(os.path.join(project, "secrets.ini"))
for name in ("wifi_ssid", "wifi_password", "ota_password"):
    value = secrets.get("secrets", name, fallback="").strip()
    if value:
        env.Append(CPPDEFINES=[(name.upper(), env.StringifyMacro(value))])  # noqa: F821
