# For `pio run -e ota -t upload` / `-t uploadfs`:
#  - the over-the-air password from secrets.ini, so it never has to be typed
#    or kept in platformio.ini (and espota's --debug, which would print it,
#    is left off);
#  - the controller's address looked up here first, with a few tries: a
#    first .local lookup can come back empty, and espota then just says
#    "Host ... Not Found".
import configparser
import os
import socket
import time

Import("env")  # noqa: F821  (SCons)

secrets = configparser.ConfigParser()
secrets.read(os.path.join(env.subst("$PROJECT_DIR"), "secrets.ini"))  # noqa: F821
password = secrets.get("secrets", "ota_password", fallback="").strip()
if not password:
    raise SystemExit("Set ota_password in secrets.ini (see secrets.example.ini) to upload over Wi-Fi")
flags = [f for f in env.get("UPLOADERFLAGS", []) if f != "--debug"]  # noqa: F821
env.Replace(UPLOADERFLAGS=flags + ["--auth=" + password])  # noqa: F821

if {"upload", "uploadfs"} & set(COMMAND_LINE_TARGETS):  # noqa: F821
    host = env.subst("$UPLOAD_PORT")  # noqa: F821
    for attempt in range(5):
        try:
            address = socket.gethostbyname(host)
            break
        except OSError:
            time.sleep(1)
    else:
        raise SystemExit(f"Couldn't find {host} on the network: is the controller on? (Or put its IP address "
                         "in upload_port in platformio.ini.)")
    print(f"Updating the controller at {host} ({address})")
    env.Replace(UPLOAD_PORT=address)  # noqa: F821
