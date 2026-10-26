# Runs after each firmware build (extra_scripts in platformio.ini): keeps a
# copy of the firmware's ELF in firmware-builds/ (gitignored), named by its
# version, build time and SHA-256, so a crash report from the controller
# (which names the crashed firmware by that SHA) can be turned into code lines
# by tools/crash_report.py, whichever build was on the piano. Gzipped; the
# newest KEEP are kept.
import glob
import gzip
import hashlib
import os
import re
import shutil

Import("env")  # noqa: F821  (SCons)

KEEP = 15
project = env.subst("$PROJECT_DIR")  # noqa: F821
folder = os.path.join(project, "firmware-builds")


def keep(source, target, env):
    elf = str(target[0])
    with open(elf, "rb") as f:
        sha = hashlib.sha256(f.read()).hexdigest()[:16]
    header = os.path.join(project, "include", "firmware_version.h")
    with open(header) as f:
        text = f.read()
    version = re.search(r'FIRMWARE_VERSION "([^"]*)"', text).group(1).replace("+", "-")
    built = re.search(r'FIRMWARE_BUILT "([^"]*)"', text).group(1).replace(" ", "_").replace(":", "-")
    os.makedirs(folder, exist_ok=True)
    if glob.glob(os.path.join(folder, f"*_{sha}.elf.gz")):
        return  # this build's already kept
    name = os.path.join(folder, f"{version}_{built}_{sha}.elf.gz")
    with open(elf, "rb") as f, gzip.open(name, "wb", compresslevel=6) as out:
        shutil.copyfileobj(f, out)
    kept = sorted(glob.glob(os.path.join(folder, "*.elf.gz")), key=os.path.getmtime)
    for old in kept[:-KEEP]:
        os.remove(old)
    print(f"Kept this build for crash reports: {os.path.relpath(name, project)}")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", keep)  # noqa: F821
