#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_DIR}/build"
APP_PATH="${BUILD_DIR}/serial-configurator"

configure() {
    cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug
}

build() {
    configure
    cmake --build "$BUILD_DIR" --parallel
}

run_app() {
    if [ ! -x "$APP_PATH" ]; then
        echo "[INFO] Binary not found, building first..."
        build
    fi

    "$APP_PATH"
}

run_tests() {
    build
    ctest --test-dir "$BUILD_DIR" --output-on-failure
}

clean() {
    rm -rf "$BUILD_DIR"
    echo "[OK] Removed ${BUILD_DIR}"
}

MODE="${1:-build}"

case "$MODE" in
    build)
        build
        ;;
    run|upload)
        build
        run_app
        ;;
    test)
        run_tests
        ;;
    clean)
        clean
        ;;
    *)
        echo "Usage: $0 {build|run|upload|test|clean}" >&2
        exit 2
        ;;
esac
