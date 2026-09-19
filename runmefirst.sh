#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$REPO_ROOT"

# Discard module build caches before bootstrap resolves the pinned HAL.
BUILD_DIRS=(
    src/ECU/build_test
    src/Clocks/build_test
    src/OilAndSpeed/build_test
    src/Adjustometer/build_test
    src/SerialConfigurator/build
    src/ECU/.build
    src/Clocks/.build
    src/OilAndSpeed/.build
    src/Adjustometer/.build
    src/Fiesta_clock/.build
)
for build_dir in "${BUILD_DIRS[@]}"; do
    if [[ -e "$build_dir" || -L "$build_dir" ]]; then
        printf 'Removing build artifacts: %s\n' "$build_dir"
        rm -rf -- "$build_dir"
    fi
done

exec ./src/ECU/scripts/bootstrap.sh
