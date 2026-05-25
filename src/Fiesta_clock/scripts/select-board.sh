#!/usr/bin/env bash
# =============================================================================
# Shared target board selector wrapper
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
COMMON_SCRIPT="$(dirname "$PROJECT_DIR")/common/scripts/fiesta-select-board.sh"

exec "$COMMON_SCRIPT" "$PROJECT_DIR" "$@"
