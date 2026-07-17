#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
SUBMODULE_PATH="subprojects/choc"
CHOC_DIR="${ROOT_DIR}/${SUBMODULE_PATH}"
CHOC_URL="https://github.com/Tracktion/choc.git"
CHOC_REVISION="$(tr -d '[:space:]' < "${ROOT_DIR}/.choc-version")"

cd "${ROOT_DIR}"

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    printf 'Error: this command must run inside a Git repository.\n' >&2
    exit 1
fi

if [[ "$(git ls-files --stage "${SUBMODULE_PATH}" | awk '{print $1}' | head -n 1)" == "160000" ]]; then
    git submodule sync -- "${SUBMODULE_PATH}"
    git submodule update --init --recursive -- "${SUBMODULE_PATH}"
else
    rm -rf "${CHOC_DIR}"
    git submodule add --force "${CHOC_URL}" "${SUBMODULE_PATH}"
fi

git -C "${CHOC_DIR}" fetch origin "${CHOC_REVISION}"
git -C "${CHOC_DIR}" checkout --detach "${CHOC_REVISION}"
git add .gitmodules .choc-version "${SUBMODULE_PATH}"

printf 'Registered upstream CHOC at %s. Commit the staged submodule pointer when ready.\n' "${CHOC_REVISION}"
