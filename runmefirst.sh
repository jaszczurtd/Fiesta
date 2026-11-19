#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_ROOT"

# shellcheck source=src/common/scripts/fiesta-modules.sh
source src/common/scripts/fiesta-modules.sh
# shellcheck source=src/common/scripts/fiesta-bootstrap-lock.sh
source src/common/scripts/fiesta-bootstrap-lock.sh

# Discard module build caches before bootstrap resolves the pinned HAL:
# every host test build and every firmware build listed in modules.json.
clean_and_bootstrap() {
    local build_dirs=() entry module build_dir
    for entry in "${FIESTA_HOST_TEST_MODULES[@]}"; do
        build_dirs+=("${entry##*:}")
    done
    for module in "${FIESTA_FIRMWARE_MODULES[@]}"; do
        build_dirs+=("src/${module}/.build")
    done
    for build_dir in "${build_dirs[@]}"; do
        if [[ -e "$build_dir" || -L "$build_dir" ]]; then
            printf 'Removing build artifacts: %s\n' "$build_dir"
            rm -rf -- "$build_dir" || return
        fi
    done
    ./src/ECU/scripts/bootstrap.sh
}

# The cleanup runs under the checkout lock, so a second run removes nothing.
status=0
fiesta_bootstrap_locked "$REPO_ROOT" clean_and_bootstrap || status=$?
exit "$status"
