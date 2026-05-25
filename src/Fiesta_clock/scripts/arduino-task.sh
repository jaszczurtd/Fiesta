#!/usr/bin/env bash
# =============================================================================
# Shared Arduino task wrapper for Fiesta_clock
# Maps legacy "build-debug" onto shared "debug" mode.
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
COMMON_SCRIPT="$(dirname "$PROJECT_DIR")/common/scripts/fiesta-arduino-task.sh"

MODE="${1:-}"
case "$MODE" in
    build)
        exec "$COMMON_SCRIPT" "$PROJECT_DIR" build
        ;;
    build-debug)
        exec "$COMMON_SCRIPT" "$PROJECT_DIR" debug
        ;;
    upload)
        exec "$COMMON_SCRIPT" "$PROJECT_DIR" upload
        ;;
    *)
        echo "Usage: $0 <build|build-debug|upload>"
        exit 2
        ;;
esac
