#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_ROOT"

# shellcheck source=src/common/scripts/fiesta-modules.sh
source src/common/scripts/fiesta-modules.sh

# Discard module build caches before bootstrap resolves the pinned HAL:
# every host test build and every firmware build listed in modules.json.
BUILD_DIRS=()
for entry in "${FIESTA_HOST_TEST_MODULES[@]}"; do
    BUILD_DIRS+=("${entry##*:}")
done
for module in "${FIESTA_FIRMWARE_MODULES[@]}"; do
    BUILD_DIRS+=("src/${module}/.build")
done
for build_dir in "${BUILD_DIRS[@]}"; do
    if [[ -e "$build_dir" || -L "$build_dir" ]]; then
        printf 'Removing build artifacts: %s\n' "$build_dir"
        rm -rf -- "$build_dir"
    fi
done

exec ./src/ECU/scripts/bootstrap.sh
