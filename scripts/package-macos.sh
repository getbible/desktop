#!/usr/bin/env bash
set -Eeuo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    printf 'Error: macOS packaging must run on macOS.\n' >&2
    exit 1
fi

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-macos}"
DIST_DIR="${DIST_DIR:-${ROOT_DIR}/dist/macos}"
CONFIG_FILE="${WEBAPP_CONFIG_FILE:-${ROOT_DIR}/config/app.json}"

if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
    BUILD_DIR="${BUILD_DIR}" WEBAPP_CONFIG_FILE="${CONFIG_FILE}" "${ROOT_DIR}/scripts/build-macos.sh"
else
    "${ROOT_DIR}/scripts/configure-app.sh" "${CONFIG_FILE}"
fi

# shellcheck disable=SC1091
source "${ROOT_DIR}/generated/app.env"

APP_BUNDLE="${BUILD_DIR}/${WEBAPP_SLUG}.app"
ENTITLEMENTS="${ROOT_DIR}/generated/macos/entitlements.plist"
DMG_PATH="${DIST_DIR}/${WEBAPP_SLUG}-${WEBAPP_VERSION}-macos.dmg"

if [[ ! -d "${APP_BUNDLE}" ]]; then
    printf 'Error: application bundle not found: %s\n' "${APP_BUNDLE}" >&2
    exit 1
fi

mkdir -p "${DIST_DIR}"
rm -f "${DMG_PATH}"

if [[ -n "${APPLE_SIGNING_IDENTITY:-}" ]]; then
    codesign \
        --force \
        --options runtime \
        --timestamp \
        --entitlements "${ENTITLEMENTS}" \
        --sign "${APPLE_SIGNING_IDENTITY}" \
        "${APP_BUNDLE}"
    codesign --verify --deep --strict --verbose=2 "${APP_BUNDLE}"
else
    printf 'Notice: APPLE_SIGNING_IDENTITY is not set; creating an unsigned DMG.\n' >&2
fi

hdiutil create \
    -volname "${WEBAPP_NAME}" \
    -srcfolder "${APP_BUNDLE}" \
    -ov \
    -format UDZO \
    "${DMG_PATH}"

if [[ "${APPLE_NOTARIZE:-0}" == "1" ]]; then
    : "${APPLE_ID:?APPLE_ID is required when APPLE_NOTARIZE=1}"
    : "${APPLE_TEAM_ID:?APPLE_TEAM_ID is required when APPLE_NOTARIZE=1}"
    : "${APPLE_APP_SPECIFIC_PASSWORD:?APPLE_APP_SPECIFIC_PASSWORD is required when APPLE_NOTARIZE=1}"
    : "${APPLE_SIGNING_IDENTITY:?APPLE_SIGNING_IDENTITY is required when APPLE_NOTARIZE=1}"

    xcrun notarytool submit "${DMG_PATH}" \
        --apple-id "${APPLE_ID}" \
        --team-id "${APPLE_TEAM_ID}" \
        --password "${APPLE_APP_SPECIFIC_PASSWORD}" \
        --wait
    xcrun stapler staple "${DMG_PATH}"
    xcrun stapler validate "${DMG_PATH}"
fi

printf 'macOS package complete: %s\n' "${DMG_PATH}"
