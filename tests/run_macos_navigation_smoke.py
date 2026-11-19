#!/usr/bin/env python3
"""Run against WKWebView on macOS after bootstrapping CHOC."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

from redirect_fixture import RedirectFixture


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="desktop-wk-redirect-") as directory:
        work = Path(directory)
        with RedirectFixture(work) as fixture:
            generated = work / "generated"
            prepared = work / "prepared-choc"
            environment = {k: v for k, v in os.environ.items() if not k.startswith("WEBAPP_")}
            subprocess.run([sys.executable, str(root / "tools/configure_app.py"), "--root", str(work),
                            "--config", str(fixture.config), "--output", str(generated)],
                           env=environment, check=True)
            subprocess.run([sys.executable, str(root / "tools/prepare_choc.py"),
                            "--source", str(root / "subprojects/choc/choc"), "--output", str(prepared)],
                           check=True)
            exe = work / "macos-navigation-smoke"
            subprocess.run(["xcrun", "clang++", "-std=c++17", "-fblocks",
                            "-I", str(generated), "-I", str(root / "src"), "-I", str(prepared),
                            str(root / "tests/macos_navigation_smoke.mm"), "-o", str(exe),
                            "-framework", "Cocoa", "-framework", "WebKit", "-framework", "Security",
                            "-framework", "CoreServices"], check=True)
            subprocess.run([str(exe), fixture.allowed_origin, fixture.blocked_origin,
                            str(fixture.cert_der)], timeout=90, check=True)
            if not fixture.redirect_seen.is_set():
                raise RuntimeError("The allowed server never received the blocked redirect request.")


if __name__ == "__main__":
    main()
