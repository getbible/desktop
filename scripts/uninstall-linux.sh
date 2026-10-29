#!/usr/bin/env bash
# Remove the selected local application, retaining its cookies, storage and cache.
set -Eeuo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG_FILE="${WEBAPP_CONFIG_FILE:-${ROOT_DIR}/config/app.json}"
PYTHON_BIN="${PYTHON_BIN:-python3}"
UNINSTALL_LOCAL=false

while (( $# )); do
    case "$1" in
        --local) UNINSTALL_LOCAL=true ;;
        --config)
            (( $# >= 2 )) || { printf 'Error: --config requires a path.\n' >&2; exit 2; }
            CONFIG_FILE="$2"
            [[ "${CONFIG_FILE}" == /* ]] || CONFIG_FILE="${PWD}/${CONFIG_FILE}"
            [[ -f "${CONFIG_FILE}" ]] || { printf 'Error: configuration not found: %s\n' "${CONFIG_FILE}" >&2; exit 2; }
            shift
            ;;
        -h|--help)
            printf 'Usage: %s --local [--config FILE]\n' "$0"
            printf 'Remove the selected WEBAPP_CONFIG_FILE application; keep saved sessions and cache.\n'
            exit 0
            ;;
        *) printf 'Error: unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

if [[ "${UNINSTALL_LOCAL}" != true ]]; then
    printf 'Error: specify --local to uninstall the selected user application.\n' >&2
    exit 2
fi

"${PYTHON_BIN}" "${ROOT_DIR}/tools/local_install.py" uninstall \
    --root "${ROOT_DIR}" --config "${CONFIG_FILE}"
