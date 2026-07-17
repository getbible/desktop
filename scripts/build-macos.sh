#!/usr/bin/env bash
set -Eeuo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    printf 'Error: the macOS application must be built on macOS.\n' >&2
    exit 1
fi

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-macos}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
CONFIG_FILE="${WEBAPP_CONFIG_FILE:-${ROOT_DIR}/config/app.json}"

"${ROOT_DIR}/scripts/bootstrap.sh"
"${ROOT_DIR}/scripts/configure-app.sh" "${CONFIG_FILE}"

# shellcheck disable=SC1091
source "${ROOT_DIR}/generated/app.env"

cmake \
    -S "${ROOT_DIR}" \
    -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DWEBAPP_USE_GENERATED=ON

cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" --parallel

APP_BUNDLE="${BUILD_DIR}/${WEBAPP_SLUG}.app"
if [[ ! -d "${APP_BUNDLE}" ]]; then
    printf 'Error: expected macOS bundle was not created: %s\n' "${APP_BUNDLE}" >&2
    exit 1
fi

printf 'macOS build complete: %s\n' "${APP_BUNDLE}"
