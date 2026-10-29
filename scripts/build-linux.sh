#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
CONFIG_FILE="${WEBAPP_CONFIG_FILE:-${ROOT_DIR}/config/app.json}"
INSTALL_LOCAL=false

while (( $# )); do
    case "$1" in
        --install|--install-local) INSTALL_LOCAL=true ;;
        --config)
            (( $# >= 2 )) || { printf 'Error: --config requires a path.\n' >&2; exit 2; }
            CONFIG_FILE="$2"
            [[ "${CONFIG_FILE}" == /* ]] || CONFIG_FILE="${PWD}/${CONFIG_FILE}"
            [[ -f "${CONFIG_FILE}" ]] || { printf 'Error: configuration not found: %s\n' "${CONFIG_FILE}" >&2; exit 2; }
            shift
            ;;
        --build-dir)
            (( $# >= 2 )) || { printf 'Error: --build-dir requires a path.\n' >&2; exit 2; }
            BUILD_DIR="$2"
            [[ "${BUILD_DIR}" == /* ]] || BUILD_DIR="${PWD}/${BUILD_DIR}"
            shift
            ;;
        -h|--help)
            printf 'Usage: %s [--config FILE] [--build-dir DIR] [--install]\n' "$0"
            printf 'Build the selected WEBAPP_CONFIG_FILE; optionally install for this user.\n'
            exit 0
            ;;
        *) printf 'Error: unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

export WEBAPP_CONFIG_FILE="${CONFIG_FILE}"

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

if [[ "${INSTALL_LOCAL}" == true ]]; then
    BUILD_DIR="${BUILD_DIR}" WEBAPP_CONFIG_FILE="${CONFIG_FILE}" \
        bash "${ROOT_DIR}/scripts/install-linux.sh" --local
fi
