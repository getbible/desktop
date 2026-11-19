#!/usr/bin/env python3
"""Exercise a real external HTTP redirect in the Windows application.

Run on Windows after scripts/build-windows.ps1 with Pillow and playwright installed.
The test rebuilds the app with a loopback-only website and a temporary profile.
"""
from __future__ import annotations

import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from urllib.error import URLError
from urllib.request import urlopen

from playwright.sync_api import Error, Page, sync_playwright

from redirect_fixture import RedirectFixture


def build_fixture_app(root: Path, work: Path, fixture: RedirectFixture) -> Path:
    # The generator writes its headers and package metadata relative to the
    # source tree. Keep both those outputs and the package being tested isolated.
    source = work / "source"
    source.mkdir()
    shutil.copy2(root / "CMakeLists.txt", source)
    for directory in ("src", "tools", "branding"):
        shutil.copytree(root / directory, source / directory)
    build = work / "build"
    environment = {k: v for k, v in os.environ.items() if not k.startswith("WEBAPP_")}
    subprocess.run(["cmake", "-S", str(source), "-B", str(build), "-A", "x64",
                    f"-DWEBAPP_CONFIG_FILE={fixture.config}",
                    f"-DWEBAPP_PYTHON_EXECUTABLE={sys.executable}",
                    f"-DCHOC_ROOT={root / 'subprojects/choc/choc'}"], env=environment, check=True)
    subprocess.run(["cmake", "--build", str(build), "--config", "Release", "--parallel"], check=True)
    config = json.loads((source / "generated/config.resolved.json").read_text(encoding="utf-8"))
    return build / "Release" / (config["app"]["slug"] + ".exe")


def certificate_pin(fixture: RedirectFixture) -> str:
    public_key = subprocess.check_output([fixture.openssl, "x509", "-in", fixture.cert,
                                          "-pubkey", "-noout"])
    spki = subprocess.check_output([fixture.openssl, "pkey", "-pubin", "-outform", "DER"],
                                   input=public_key)
    return base64.b64encode(hashlib.sha256(spki).digest()).decode("ascii")


def wait_for_webview(endpoint: str, process: subprocess.Popen) -> None:
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Desktop application exited during startup ({process.returncode})")
        try:
            with urlopen(endpoint + "/json/list", timeout=1) as response:
                if any(target["type"] == "page" for target in json.load(response)):
                    return
        except (URLError, OSError):
            pass
        time.sleep(0.1)
    raise RuntimeError("The desktop application's WebView2 did not expose a page within 60 seconds")


def exercise(page: Page, fixture: RedirectFixture) -> None:
    page.set_default_timeout(15000)
    home = fixture.allowed_origin + "/"
    # Wait for the app's own startup navigation to reach its configured website.
    # An explicit page.goto() here would sidestep the native shell's startup.
    try:
        page.locator("#unsaved").wait_for(timeout=30000)
    except Error as error:
        raise AssertionError(f"Native startup did not reach {home}; current URL is {page.url}") from error
    assert page.url == home
    # Follow the public shell URLs a hosted website receives, including a
    # relative resource and the links that take the user back to the website.
    page.evaluate("location.assign(window.WebAppDesktop.aboutURL)")
    page.get_by_role("link", name="Open application").wait_for()
    assert page.url == "webapp://app/about"
    page.wait_for_function("document.images[0]?.complete && document.images[0].naturalWidth > 0")
    page.get_by_role("link", name="Support", exact=True).click()
    page.get_by_role("link", name="Back to application").wait_for()
    assert page.url == "webapp://app/support"
    page.get_by_role("link", name="Back to application").click()
    page.locator("#unsaved").wait_for()
    assert page.url == home
    page.locator("#unsaved").fill("unfinished note")
    # A reload, a replacement document or a return home destroys this handle.
    original = page.evaluate_handle("document")
    try:
        page.goto(fixture.allowed_origin + "/redirect-blocked", wait_until="commit")
    except Error as error:
        # WebView2 reports a native NavigationStarting cancellation to Chromium.
        if not any(code in str(error) for code in ("net::ERR_ABORTED", "net::ERR_BLOCKED_BY_CLIENT")):
            raise
    else:
        raise AssertionError("The external redirect committed a new document")
    assert fixture.redirect_seen.is_set(), "The allowed server never received the redirect request"
    assert page.url == home, f"The original URL changed to {page.url}"
    assert original.evaluate("value => value === document"), "The original document was replaced"
    assert page.locator("#unsaved").input_value() == "unfinished note", "The draft was lost"
    original.dispose()
    page.goto(fixture.allowed_origin + "/redirect-allowed")
    assert page.url == fixture.allowed_origin + "/destination"
    page.locator("#destination").wait_for()


def main() -> None:
    if sys.platform != "win32":
        raise SystemExit("Run this WebView2 smoke test on Windows.")
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="desktop-redirect-") as temporary:
        work = Path(temporary)
        with RedirectFixture(work) as fixture:
            executable = build_fixture_app(root, work, fixture)
            with socket.socket() as reservation:
                reservation.bind(("127.0.0.1", 0))
                port = reservation.getsockname()[1]
            endpoint = f"http://127.0.0.1:{port}"
            environment = {key: value for key, value in os.environ.items()
                           if not key.startswith(("WEBAPP_", "WEBVIEW2_"))}
            environment.update(TMP=str(work), TEMP=str(work), WEBAPP_DEBUG="1",
                               WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=(
                                   f"--remote-debugging-port={port} "
                                   f"--ignore-certificate-errors-spki-list={certificate_pin(fixture)}"))
            with subprocess.Popen([executable], env=environment) as process:
                try:
                    wait_for_webview(endpoint, process)
                    with sync_playwright() as playwright:
                        browser = playwright.chromium.connect_over_cdp(endpoint)
                        exercise(browser.contexts[0].pages[0], fixture)
                finally:
                    if process.poll() is None:
                        # Let the application close its controller and release the temporary profile.
                        subprocess.run(["powershell", "-NoProfile", "-Command",
                                        f"(Get-Process -Id {process.pid}).CloseMainWindow() | Out-Null"],
                                       check=False)
                        try:
                            process.wait(timeout=15)
                        except subprocess.TimeoutExpired:
                            subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)], check=True)
    print("WebView2 preserved the unsaved document after a blocked redirect and followed an allowed redirect.")


if __name__ == "__main__":
    main()
