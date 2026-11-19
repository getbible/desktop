"""Two loopback HTTPS origins used by the native redirect smoke tests."""
from __future__ import annotations

from contextlib import ExitStack
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import ssl
import subprocess
import threading


class RedirectFixture:
    def __init__(self, work: Path):
        self.cert = work / "fixture.pem"
        self.cert_der = work / "fixture.der"
        self.config = work / "app.json"
        self.redirect_seen = threading.Event()
        self._stack = ExitStack()

    def __enter__(self) -> RedirectFixture:
        try:
            self._start()
        except BaseException:
            self._stack.close()
            raise
        return self

    def __exit__(self, *_: object) -> None:
        self._stack.close()

    def _start(self) -> None:
        root = Path(__file__).resolve().parents[1]
        git_openssl = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Git/usr/bin/openssl.exe"
        self.openssl = shutil.which("openssl") or str(git_openssl)
        key = self.cert.with_suffix(".key")
        cert_config = self.cert.with_suffix(".cnf")
        cert_config.write_text("[req]\ndistinguished_name=dn\nx509_extensions=leaf\nprompt=no\n"
                               "[dn]\nCN=127.0.0.1\n[leaf]\nsubjectAltName=IP:127.0.0.1\n"
                               "extendedKeyUsage=serverAuth\n", encoding="ascii")
        subprocess.run([self.openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-sha256", "-config", str(cert_config),
                        "-keyout", str(key), "-out", str(self.cert)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        self.cert_der.write_bytes(ssl.PEM_cert_to_DER_cert(self.cert.read_text()))
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(self.cert, key)
        fixture = self

        class Page(BaseHTTPRequestHandler):
            def do_GET(self) -> None:
                if self.path in ("/redirect-blocked", "/redirect-allowed"):
                    origin = fixture.allowed_origin
                    if self.path == "/redirect-blocked":
                        origin = fixture.blocked_origin
                        fixture.redirect_seen.set()
                    self.send_response(302)
                    self.send_header("Location", origin + "/destination")
                    body = b""
                elif self.path in ("/", "/destination"):
                    self.send_response(200)
                    self.send_header("Content-Type", "text/html; charset=utf-8")
                    body = (b'<!doctype html><textarea id="unsaved"></textarea>' if self.path == "/"
                            else b'<!doctype html><p id="destination">Destination</p>')
                else:
                    self.send_error(404)
                    return
                self.send_header("Cache-Control", "no-store")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *_: object) -> None:
                pass

        def start_server() -> str:
            server = ThreadingHTTPServer(("127.0.0.1", 0), Page)
            self._stack.callback(server.server_close)
            server.socket = tls.wrap_socket(server.socket, server_side=True)
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            self._stack.callback(thread.join)
            self._stack.callback(server.shutdown)
            return f"https://127.0.0.1:{server.server_port}"

        self.allowed_origin = start_server()
        self.blocked_origin = start_server()
        config = json.loads((root / "config/app.json").read_text(encoding="utf-8"))
        config["app"].update(id="org.example.desktopredirect", slug="desktop-redirect-test",
                             persistent=False, url=self.allowed_origin + "/",
                             homepage_url=self.allowed_origin + "/", allowed_origins=[self.allowed_origin])
        config["branding"]["icon"] = str(root / config["branding"]["icon"])
        self.config.write_text(json.dumps(config), encoding="utf-8")
