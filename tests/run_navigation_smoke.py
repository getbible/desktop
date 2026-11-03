#!/usr/bin/env python3
"""Verify production navigation/reconnection handlers using native WebKitGTK.

Run after building the app (or generating its metadata) and bootstrapping CHOC:
  dbus-run-session -- xvfb-run -a python3 tests/run_navigation_smoke.py
The fixture uses an in-process custom scheme and makes no external requests.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--choc-root", type=Path, default=root / "subprojects/choc/choc")
    args = parser.parse_args()
    if not os.environ.get("DISPLAY"):
        raise SystemExit("A display is required; run with xvfb-run -a.")
    if not (root / "generated/app_config.hpp").is_file():
        raise SystemExit("Generate application metadata first with bash scripts/configure-app.sh.")
    flags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "gtk+-3.0", "webkit2gtk-4.1"], text=True))
    with tempfile.TemporaryDirectory(prefix="desktop-navigation-smoke-") as temporary:
        work = Path(temporary)
        prepared = work / "prepared-choc"
        subprocess.run([sys.executable, str(root / "tools/prepare_choc.py"),
                        "--source", str(args.choc_root.resolve()), "--output", str(prepared)], check=True)
        executable = work / "navigation-smoke"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra",
                        str(root / "tests/navigation_smoke.cpp"), "-I", str(root / "generated"),
                        "-I", str(root / "src"), "-I", str(prepared),
                        "-o", str(executable), *flags, "-pthread"], check=True)
        environment = dict(os.environ)
        environment.update(XDG_DATA_HOME=str(work / "data"), XDG_CACHE_HOME=str(work / "cache"),
                           WEBKIT_DISABLE_COMPOSITING_MODE="1")
        subprocess.run([str(executable)], env=environment, check=True, timeout=30)


if __name__ == "__main__":
    main()
