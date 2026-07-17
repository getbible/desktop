#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG_FILE="${1:-${WEBAPP_CONFIG_FILE:-${ROOT_DIR}/config/app.json}}"
PYTHON_BIN="${PYTHON_BIN:-python3}"

if ! command -v "${PYTHON_BIN}" >/dev/null 2>&1; then
    printf 'Error: %s is required to generate the application configuration.\n' "${PYTHON_BIN}" >&2
    exit 1
fi

if ! "${PYTHON_BIN}" -c 'import PIL' >/dev/null 2>&1; then
    printf 'Error: Pillow is required. Install python3-pil or run:\n' >&2
    printf '  python3 -m pip install -r %s/tools/requirements.txt\n' "${ROOT_DIR}" >&2
    exit 1
fi

"${PYTHON_BIN}" \
    "${ROOT_DIR}/tools/configure_app.py" \
    --root "${ROOT_DIR}" \
    --config "${CONFIG_FILE}" \
    --output "${ROOT_DIR}/generated"
