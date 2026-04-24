#!/usr/bin/env bash
# =============================================================================
# Refresh IntelliSense configuration
# Delegates to the shared Fiesta implementation
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
COMMON_SCRIPT="$(dirname "$PROJECT_DIR")/common/scripts/fiesta-refresh-intellisense.sh"

exec "$COMMON_SCRIPT" "$PROJECT_DIR" "$@"
