#!/usr/bin/env python3
"""Verify native recovery controls using an isolated profile and loopback HTTP.

Run on Linux after bootstrap with GTK/WebKit development packages installed:
  dbus-run-session -- xvfb-run -a python3 tests/run_recovery_smoke.py
"""
from __future__ import annotations

import argparse
import http.server
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import threading


class Page(http.server.BaseHTTPRequestHandler):
    generation = 0

    def do_GET(self) -> None:
        type(self).generation += 1
        body = (f"<!doctype html><html><head><title>Recovery fixture</title></head>"
                f'<body data-generation="{type(self).generation}" style="font:18px sans-serif;padding:24px">'
                "<h1>Website content</h1><p>Refresh keeps your login. Reset asks before signing you out.</p>"
                '<label>Unsaved draft<br><textarea rows="4" cols="48">Example draft</textarea></label>'
                "</body></html>").encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        # A deliberately cacheable page lets the test prove Refresh bypasses it.
        self.send_header("Cache-Control", "public, max-age=3600")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_: object) -> None:
        pass


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--choc-root", type=Path, default=root / "subprojects/choc/choc")
    parser.add_argument("--screenshot", type=Path)
    args = parser.parse_args()
    if not os.environ.get("DISPLAY"):
        raise SystemExit("A display is required; run with xvfb-run -a.")
    flags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "gtk+-3.0", "webkit2gtk-4.1"], text=True))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Page)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    environment = {key: value for key, value in os.environ.items() if not key.startswith("WEBAPP_")}
    try:
        with tempfile.TemporaryDirectory(prefix="desktop-recovery-smoke-") as temporary:
            work = Path(temporary)
            source = work / "fixture-source"
            source.mkdir()
            configuration = json.loads((root / "config/app.json").read_text())
            configuration["app"].update(name="Recovery fixture", slug="recovery-fixture",
                id="org.example.desktoprecovery", url="https://recovery.invalid/",
                homepage_url="https://recovery.invalid/", allowed_origins=["https://recovery.invalid"])
            configuration["branding"]["icon"] = str(root / "branding/getbible-life.png.base64")
            config_file = source / "app.json"
            config_file.write_text(json.dumps(configuration))
            generated = work / "generated"
            subprocess.run([os.sys.executable, str(root / "tools/configure_app.py"),
                            "--root", str(source), "--config", str(config_file),
                            "--output", str(generated)], env=environment, check=True)
            # Production configs require HTTPS. The fixture uses loopback HTTP
            # without installing any production navigation guards or TLS bypass.
            header = generated / "app_config.hpp"
            header.write_text(header.read_text().replace("https://recovery.invalid",
                              f"http://127.0.0.1:{server.server_port}"))
            prepared = work / "prepared-choc"
            subprocess.run([os.sys.executable, str(root / "tools/prepare_choc.py"),
                            "--source", str(args.choc_root.resolve()), "--output", str(prepared)], check=True)
            environment.update(XDG_DATA_HOME=str(work / "data"), XDG_CACHE_HOME=str(work / "cache"),
                               WEBKIT_DISABLE_COMPOSITING_MODE="1")
            if args.screenshot:
                environment["WEBAPP_RECOVERY_SCREENSHOT"] = str(args.screenshot.resolve())
            for profile in ("isolated", "legacy-shared"):
                if profile == "legacy-shared":
                    header.write_text(header.read_text().replace("org.example.desktoprecovery", "live.getbible.app"))
                executable = work / f"recovery-smoke-{profile}"
                subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
                                str(root / "tests/recovery_smoke.cpp"), "-I", str(generated),
                                "-I", str(root / "src"), "-I", str(prepared), "-o", str(executable),
                                *flags, "-pthread"], check=True)
                for operation in ("exercise", "verify-empty"):
                    result = subprocess.run([str(executable), operation], env=environment,
                                            text=True, capture_output=True, timeout=45)
                    print(result.stdout, end="", flush=True)
                    if result.returncode:
                        raise RuntimeError(f"Native recovery {profile}/{operation} failed:\n{result.stdout}{result.stderr}")
            print("Native Refresh, Reset confirmation/cancel/failure, scoped reset, and restart checks passed.")
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)


if __name__ == "__main__":
    main()
