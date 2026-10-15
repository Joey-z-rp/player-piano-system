"""The review page (calibrate_dynamics.py review): each key's strikes on
charts, played from their recordings, and flags set by ear. Served from this
computer only."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from calibration_analysis import build_mapping  # noqa: E402
from calibration_common import note_name  # noqa: E402
from calibration_data import group_rows, load_flags, load_raw, reanalyse  # noqa: E402


def review(args) -> None:
    """Serves the review page (calibration_review.html) on this computer: each
    key's strikes on charts, played from their recordings when clicked, and
    flags you set by ear, saved to FLAGS_FILE."""
    import http.server
    import io
    import wave
    import webbrowser
    from urllib.parse import urlparse

    rows = load_raw(args.raw)
    print("Analysing the recordings (about a minute)...", flush=True)
    reanalyse(rows, args.recordings, args.margin_db)
    state = {"flags": load_flags(args.flags)}

    def regroup() -> None:
        state["by_key"] = group_rows(rows, state["flags"], args.cfg.IGNORE_FIRST_STRIKE)
        state["mapping"] = build_mapping(state["by_key"], args, state["flags"])

    regroup()
    first = {}
    for row in rows:
        first[row["key"]] = min(first.get(row["key"], row["repeat"]), row["repeat"])

    def key_data(key: int) -> dict:
        m = state["mapping"].get(key)
        strikes = state["flags"]["strikes"]
        return {
            "key": key,
            "note": note_name(key),
            "keys": sorted({r["key"] for r in rows}),
            "summary": None if m is None else {
                "threshold": m["threshold"], "min": m["min"], "max": m["max"], "curve": m["curve"],
                "landing": m["landing"], "decay_db": m["decay_db"], "damped": m["damped"], "bounced": m["bounced"],
            },
            "flags": state["flags"]["keys"].get(str(key), {}),
            "strikes": [
                {
                    "repeat": r["repeat"], "duty": r["duty"], "result": r.get("result", "hit" if r["hit"] else "silent"),
                    "verdict": strikes.get(f"{key}:{r['repeat']}"), "peak_db": round(r["peak_db"], 2),
                    "lands_ms": r["lands_ms"], "decay_db": None if np.isnan(r["decay_db"]) else round(r["decay_db"], 1),
                    "bounce": r["bounce"], "ignored": args.cfg.IGNORE_FIRST_STRIKE and r["repeat"] == first[key],
                    "command": r["command"],
                }
                for r in sorted((r for r in rows if r["key"] == key), key=lambda r: r["repeat"])
            ],
        }

    audio_cache: dict[int, tuple[int, dict]] = {}

    def wav(key: int, repeat: int) -> bytes:
        if key not in audio_cache:
            saved = np.load(args.recordings / f"key_{key:02d}.npz")
            audio_cache.clear()  # one key's recordings at a time (~8 MB)
            audio_cache[key] = (int(saved["sample_rate"]), dict(zip(saved["repeat"].tolist(), saved["audio"])))
        rate, audio = audio_cache[key]
        out = io.BytesIO()
        with wave.open(out, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(rate)
            w.writeframes(audio[repeat].astype("<i2").tobytes())
        return out.getvalue()

    page = (Path(__file__).resolve().parent / "calibration_review.html").read_bytes()

    class Handler(http.server.BaseHTTPRequestHandler):
        def send(self, status: int, body: bytes, kind: str) -> None:
            self.send_response(status)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def send_json(self, data) -> None:
            self.send(200, json.dumps(data).encode(), "application/json")

        def do_GET(self) -> None:
            parts = urlparse(self.path).path.strip("/").split("/")
            try:
                if parts == [""]:
                    self.send(200, page, "text/html; charset=utf-8")
                elif parts[0] == "key" and len(parts) == 2:
                    self.send_json(key_data(int(parts[1])))
                elif parts[0] == "audio" and len(parts) == 3:
                    self.send(200, wav(int(parts[1]), int(parts[2].removesuffix(".wav"))), "audio/wav")
                else:
                    self.send(404, b"not found", "text/plain")
            except (KeyError, ValueError, FileNotFoundError) as error:
                self.send(404, str(error).encode(), "text/plain")

        def do_POST(self) -> None:
            body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
            key = int(body["key"])
            flags = state["flags"]
            if "repeat" in body:  # a strike's verdict: "note", "thump", or null for automatic
                name = f"{key}:{int(body['repeat'])}"
                if body.get("verdict") in ("note", "thump"):
                    flags["strikes"][name] = body["verdict"]
                else:
                    flags["strikes"].pop(name, None)
            else:  # the key's flags
                kept = {k: body[k] for k in ("damped", "bounces", "comment") if body.get(k)}
                if kept:
                    flags["keys"][str(key)] = kept
                else:
                    flags["keys"].pop(str(key), None)
            args.flags.parent.mkdir(parents=True, exist_ok=True)
            temp = args.flags.with_suffix(".tmp")
            temp.write_text(json.dumps(flags, indent=1, sort_keys=True) + "\n")
            temp.replace(args.flags)
            regroup()
            self.send_json(key_data(key))

        def log_message(self, *_args) -> None:
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    url = f"http://127.0.0.1:{args.port}/"
    print(f"Review page at {url} (flags are saved to {args.flags}); Ctrl-C to stop", flush=True)
    if args.open:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")

