#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <project-dir> <uf2-path> [port] [verbose]" >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
source "$SCRIPT_DIR/fiesta-arduino-common.sh"

PROJECT_DIR="$1"
UF2_PATH="$2"
PORT="${3:-}"
VERBOSE="${4:-0}"

MANIFEST="$(fiesta_prepare_manifest_for_uf2 "$PROJECT_DIR" "$UF2_PATH")"
echo "Manifest ready: $MANIFEST"
echo "Uploading exact UF2 selected by manifest gate: $UF2_PATH"

fiesta_run_upload_from_file "$PROJECT_DIR" "$UF2_PATH" "$PORT" "$VERBOSE"
