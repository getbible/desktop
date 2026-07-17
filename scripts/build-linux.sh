#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
CONFIG_FILE="${WEBAPP_CONFIG_FILE:-${ROOT_DIR}/config/app.json}"

bash "${ROOT_DIR}/scripts/bootstrap.sh"
bash "${ROOT_DIR}/scripts/configure-app.sh" "${CONFIG_FILE}"

# shellcheck disable=SC1091
source "${ROOT_DIR}/generated/app.env"

cmake \
    -S "${ROOT_DIR}" \
    -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DWEBAPP_USE_GENERATED=ON

cmake --build "${BUILD_DIR}" --parallel

printf 'Build complete: %s/%s\n' "${BUILD_DIR}" "${WEBAPP_EXECUTABLE}"
