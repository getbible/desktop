#!/usr/bin/env python3
"""Exercise native Linux browser storage against a loopback-only HTTP fixture.

Run after scripts/bootstrap.sh on a host with GTK/WebKit development packages:
  xvfb-run -a python3 tests/run_storage_smoke.py
"""
from __future__ import annotations

import argparse
import http.server
import os
from pathlib import Path
import shlex
import stat
import subprocess
import tempfile
import threading
import time


class Page(http.server.BaseHTTPRequestHandler):
    def do_GET(self) -> None:
        body = b"<!doctype html><html><title>Storage smoke test</title><body>Local test</body></html>"
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_: object) -> None:
        pass


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--choc-root", type=Path, default=root / "subprojects/choc/choc")
    args = parser.parse_args()
    if not os.environ.get("DISPLAY"):
        raise SystemExit("A display is required; run with xvfb-run -a.")
    flags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "gtk+-3.0", "webkit2gtk-4.1"], text=True))

    with tempfile.TemporaryDirectory(prefix="desktop-storage-smoke-") as temporary:
        work = Path(temporary)
        (work / "app_config.hpp").write_text(
            "#pragma once\n#include <string_view>\nnamespace webapp::config {\n"
            "inline std::string_view appID;\ninline bool persistent = true;\n}\n")
        executable = work / "storage-smoke"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
                        str(root / "tests/storage_smoke.cpp"), "-I", str(work),
                        "-I", str(root / "src"), "-I", str(args.choc_root.resolve()),
                        "-o", str(executable), *flags], check=True)
        environment = dict(os.environ)
        environment.update(XDG_DATA_HOME=str(work / "data"), XDG_CACHE_HOME=str(work / "cache"),
                           WEBKIT_DISABLE_COMPOSITING_MODE="1")
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Page)
        server.daemon_threads = True
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        uri = f"http://127.0.0.1:{server.server_port}/"

        def run(app_id: str, persistent: bool, operation: str, retry: bool = False) -> None:
            deadline = time.monotonic() + (15 if retry else 0)
            while True:
                result = subprocess.run([str(executable), app_id, str(persistent).lower(), operation, uri],
                                        env=environment, text=True, capture_output=True, timeout=30)
                if result.returncode == 0:
                    print(f"PASS {app_id}: persistent={persistent}, {operation}", flush=True)
                    return
                if not retry or time.monotonic() >= deadline:
                    raise RuntimeError(f"{app_id} {operation} failed:\n{result.stdout}{result.stderr}")
                # WebKit's separate storage process may finish an asynchronous
                # disk write just after the previous UI process closes.
                time.sleep(0.2)

        try:
            first = "org.example.desktopstorage.first"
            second = "org.example.desktopstorage.second"
            run(first, True, "empty")
            run(first, True, "write")
            run(first, True, "read", retry=True)
            run(second, True, "empty")
            run(second, True, "write")
            run(second, True, "read", retry=True)
            # Ephemeral mode must ignore the same app ID's saved profile.
            run(first, False, "empty")
            run(first, False, "write")
            run(first, False, "empty")
            # Ephemeral use must not delete or replace persistent data.
            run(first, True, "read")
            for app_id in (first, second):
                data = work / "data" / app_id
                cache = work / "cache" / app_id
                assert (data / "cookies.sqlite").is_file(), f"Cookie database missing for {app_id}"
                assert stat.S_IMODE(data.stat().st_mode) == 0o700, f"Unsafe data permissions for {app_id}"
                assert stat.S_IMODE(cache.stat().st_mode) == 0o700, f"Unsafe cache permissions for {app_id}"
            print("Native cookies/localStorage restart, profile isolation, and ephemeral-mode checks passed.")
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)


if __name__ == "__main__":
    main()
