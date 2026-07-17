#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
PREFIX="${PREFIX:-/usr/local}"
CONFIG_FILE="${WEBAPP_CONFIG_FILE:-${ROOT_DIR}/config/app.json}"

"${ROOT_DIR}/scripts/configure-app.sh" "${CONFIG_FILE}"
# shellcheck disable=SC1091
source "${ROOT_DIR}/generated/app.env"

if [[ ! -x "${BUILD_DIR}/${WEBAPP_EXECUTABLE}" ]]; then
    "${ROOT_DIR}/scripts/build-linux.sh"
fi

cmake --install "${BUILD_DIR}" --prefix "${PREFIX}"
printf '%s installed under %s.\n' "${WEBAPP_NAME}" "${PREFIX}"
