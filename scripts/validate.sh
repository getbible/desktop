#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-python3}"

cd "${ROOT_DIR}"

"${ROOT_DIR}/scripts/configure-app.sh" "${WEBAPP_CONFIG_FILE:-${ROOT_DIR}/config/app.json}"
"${PYTHON_BIN}" -m unittest discover -s "${ROOT_DIR}/tests" -p 'test_*.py' -v

"${PYTHON_BIN}" - <<'PY'
from pathlib import Path
import json
import xml.etree.ElementTree as ET
from PIL import Image

root = Path.cwd()
resolved = json.loads((root / "generated/config.resolved.json").read_text(encoding="utf-8"))
xml_paths = [
    root / "generated/Info.plist",
    root / "generated/macos/entitlements.plist",
    root / "generated/windows/AppxManifest.xml",
    *(root / "generated/packaging/linux").glob("*.xml"),
]
for xml_path in xml_paths:
    ET.parse(xml_path)

windows_assets = {
    "Square44x44Logo.png": (44, 44),
    "Square150x150Logo.png": (150, 150),
    "Square310x310Logo.png": (310, 310),
    "StoreLogo.png": (50, 50),
    "Wide310x150Logo.png": (310, 150),
}
for filename, dimensions in windows_assets.items():
    with Image.open(root / "generated/windows/Assets" / filename) as image:
        if image.size != dimensions:
            raise SystemExit(f"{filename} has {image.size}, expected {dimensions}")

if resolved["app"]["name"] == "getBible.Life":
    expected_url = "https://app.getbible.life/"
    if resolved["app"]["url"] != expected_url or resolved["app"]["homepage_url"] != expected_url:
        raise SystemExit("The default GetBible URL and homepage must both use https://app.getbible.life/.")
    if resolved["app"]["id"] != "live.getbible.app":
        raise SystemExit("The default GetBible application ID must be live.getbible.app.")
    if resolved["window"]["show_app_menu"]:
        raise SystemExit("The floating application menu must remain disabled in the default profile.")

print("Generated JSON, XML, platform assets, and default identity are valid.")
PY

bash -n "${ROOT_DIR}"/scripts/*.sh
printf 'Validation complete.\n'
