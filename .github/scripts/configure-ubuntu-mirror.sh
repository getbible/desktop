#!/usr/bin/env bash
set -euo pipefail

# Pick a reachable archive for this disposable Ubuntu runner. Keep its suites,
# components and Signed-By keyring for both host installs and Snapcraft staging.
sources=/etc/apt/sources.list.d/ubuntu.sources
if [[ -f "$sources" ]]; then
    source /etc/os-release
    # Some mirrors served the signed index but returned 503 on package files.
    package=$(apt-cache show libglx0 2>/dev/null | awk '/^Filename: pool\// && !seen++ {print $2}' || true)
    for mirror in https://mirrors.kernel.org/ubuntu https://mirror.math.princeton.edu/pub/ubuntu https://archive.ubuntu.com/ubuntu; do
        if curl --fail-early --fail --silent --show-error --location --max-time 12 --range 0-127 \
            --output /dev/null "$mirror/dists/$VERSION_CODENAME-security/InRelease" \
            --output /dev/null "$mirror/${package:-pool/main/libg/libglvnd/}"; then
            echo "Using Ubuntu package mirror $mirror"
            sudo sed -i "s|^URIs:.*|URIs: $mirror|" "$sources"
            break
        fi
    done
fi

sudo apt-get update
