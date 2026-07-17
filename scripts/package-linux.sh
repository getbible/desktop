#!/usr/bin/env bash
set -Eeuo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
    printf 'Error: Linux packaging must run on Linux.\n' >&2
    exit 1
fi

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-linux}"
DIST_DIR="${DIST_DIR:-${ROOT_DIR}/dist/linux}"
CONFIG_FILE="${WEBAPP_CONFIG_FILE:-${ROOT_DIR}/config/app.json}"

if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
    BUILD_DIR="${BUILD_DIR}" WEBAPP_CONFIG_FILE="${CONFIG_FILE}" "${ROOT_DIR}/scripts/build-linux.sh"
else
    "${ROOT_DIR}/scripts/configure-app.sh" "${CONFIG_FILE}"
fi

# shellcheck disable=SC1091
source "${ROOT_DIR}/generated/app.env"

EXECUTABLE="${BUILD_DIR}/${WEBAPP_SLUG}"
DESKTOP_FILE="${ROOT_DIR}/generated/packaging/linux/${WEBAPP_ID}.desktop"
ICON_FILE="${ROOT_DIR}/generated/assets/icon-256.png"
ARCHIVE_ROOT="${WEBAPP_SLUG}-${WEBAPP_VERSION}-linux-$(uname -m)"
STAGE_DIR="${DIST_DIR}/${ARCHIVE_ROOT}"
ARCHIVE_PATH="${DIST_DIR}/${ARCHIVE_ROOT}.tar.gz"

if [[ ! -x "${EXECUTABLE}" ]]; then
    printf 'Error: executable not found: %s\n' "${EXECUTABLE}" >&2
    exit 1
fi

rm -rf "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}"
cp "${EXECUTABLE}" "${STAGE_DIR}/${WEBAPP_SLUG}"
cp "${DESKTOP_FILE}" "${STAGE_DIR}/${WEBAPP_ID}.desktop"
cp "${ICON_FILE}" "${STAGE_DIR}/${WEBAPP_SLUG}.png"

cat > "${STAGE_DIR}/README.txt" <<README
${WEBAPP_NAME} ${WEBAPP_VERSION}

Target website: ${WEBAPP_URL}

Run directly:
  ./${WEBAPP_SLUG}

Required runtime libraries on Debian/Ubuntu:
  sudo apt install libgtk-3-0 libwebkit2gtk-4.1-0

For a fully confined Linux installation, use the .snap package from the same
GitHub release when available.
README

rm -f "${ARCHIVE_PATH}"
tar -C "${DIST_DIR}" -czf "${ARCHIVE_PATH}" "${ARCHIVE_ROOT}"
rm -rf "${STAGE_DIR}"

printf 'Linux package complete: %s\n' "${ARCHIVE_PATH}"
