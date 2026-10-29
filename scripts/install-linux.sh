#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"
PREFIX="${PREFIX:-/usr/local}"
CONFIG_FILE="${WEBAPP_CONFIG_FILE:-${ROOT_DIR}/config/app.json}"
INSTALL_LOCAL=false
PYTHON_BIN="${PYTHON_BIN:-python3}"

while (( $# )); do
    case "$1" in
        --local) INSTALL_LOCAL=true ;;
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
            printf 'Usage: %s [--config FILE] [--build-dir DIR] [--local]\n' "$0"
            printf 'Default: install with CMake under PREFIX (default /usr/local).\n'
            printf -- '--local: install under ~/.local/bin/images and the XDG desktop directories.\n'
            exit 0
            ;;
        *) printf 'Error: unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

export WEBAPP_CONFIG_FILE="${CONFIG_FILE}"

bash "${ROOT_DIR}/scripts/configure-app.sh" "${CONFIG_FILE}"
# shellcheck disable=SC1091
source "${ROOT_DIR}/generated/app.env"

if [[ ! -x "${BUILD_DIR}/${WEBAPP_EXECUTABLE}" ]]; then
    BUILD_DIR="${BUILD_DIR}" WEBAPP_CONFIG_FILE="${CONFIG_FILE}" \
        bash "${ROOT_DIR}/scripts/build-linux.sh"
fi

if [[ "${INSTALL_LOCAL}" == true ]]; then
    "${PYTHON_BIN}" "${ROOT_DIR}/tools/local_install.py" install \
        --root "${ROOT_DIR}" --config "${CONFIG_FILE}" --build-dir "${BUILD_DIR}"
else
    cmake --install "${BUILD_DIR}" --prefix "${PREFIX}"
    printf '%s installed under %s.\n' "${WEBAPP_NAME}" "${PREFIX}"
fi
