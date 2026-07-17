#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
SUBMODULE_PATH="subprojects/choc"
CHOC_DIR="${ROOT_DIR}/${SUBMODULE_PATH}"
VERSION_FILE="${ROOT_DIR}/.choc-version"
GITMODULES_FILE="${ROOT_DIR}/.gitmodules"

if [[ ! -f "${GITMODULES_FILE}" ]]; then
    printf 'Error: %s is missing.\n' "${GITMODULES_FILE}" >&2
    exit 1
fi

if [[ ! -f "${VERSION_FILE}" ]]; then
    printf 'Error: %s is missing.\n' "${VERSION_FILE}" >&2
    exit 1
fi

CHOC_URL="$(git config -f "${GITMODULES_FILE}" --get "submodule.${SUBMODULE_PATH}.url")"
CHOC_REVISION="$(tr -d '[:space:]' < "${VERSION_FILE}")"

if [[ -z "${CHOC_URL}" || -z "${CHOC_REVISION}" ]]; then
    printf 'Error: the CHOC URL or pinned revision is empty.\n' >&2
    exit 1
fi

if git -C "${ROOT_DIR}" rev-parse --is-inside-work-tree >/dev/null 2>&1 \
    && [[ "$(git -C "${ROOT_DIR}" ls-files --stage "${SUBMODULE_PATH}" | awk '{print $1}' | head -n 1)" == "160000" ]]; then
    git -C "${ROOT_DIR}" submodule sync -- "${SUBMODULE_PATH}"
    git -C "${ROOT_DIR}" submodule update --init --recursive -- "${SUBMODULE_PATH}"
else
    if [[ ! -d "${CHOC_DIR}/.git" ]]; then
        rm -rf "${CHOC_DIR}"
        mkdir -p "${CHOC_DIR}"
        git -C "${CHOC_DIR}" init
        git -C "${CHOC_DIR}" remote add origin "${CHOC_URL}"
    fi

    git -C "${CHOC_DIR}" fetch --depth 1 origin "${CHOC_REVISION}"
    git -C "${CHOC_DIR}" checkout --detach FETCH_HEAD
fi

if [[ ! -f "${CHOC_DIR}/choc/gui/choc_WebView.h" ]]; then
    printf 'Error: CHOC was fetched, but choc_WebView.h was not found.\n' >&2
    exit 1
fi

printf 'CHOC is ready at %s (%s).\n' "${CHOC_DIR}" "${CHOC_REVISION}"
