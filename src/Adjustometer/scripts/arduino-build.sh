#!/usr/bin/env bash
# =============================================================================
# Shared Arduino build wrapper for VS Code tasks
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
COMMON_SCRIPT="$(dirname "$PROJECT_DIR")/common/scripts/fiesta-arduino-task.sh"

exec "$COMMON_SCRIPT" "$PROJECT_DIR" "$@"