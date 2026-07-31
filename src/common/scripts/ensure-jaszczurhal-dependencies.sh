#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 JASZCZURHAL_DIR {host|rp}"
}

if [[ $# -ne 2 ]]; then
    usage >&2
    exit 2
fi

JASZCZURHAL_DIR="$1"
PROFILE="$2"

case "$PROFILE" in
    host|rp) ;;
    *)
        usage >&2
        exit 2
        ;;
esac

if [[ ! -d "$JASZCZURHAL_DIR/scripts" ]]; then
    echo "JaszczurHAL scripts directory not found: $JASZCZURHAL_DIR/scripts" >&2
    exit 1
fi

SOURCE_DEPENDENCIES=(
    bearssl
    cjson
    lodepng
    jpeg
    fatfs
    unity
    lwip
    littlefs
)

ensure_component() {
    local component="$1"
    shift

    local ensure_script="$JASZCZURHAL_DIR/scripts/ensure_${component}.sh"
    if [[ ! -x "$ensure_script" ]]; then
        echo "JaszczurHAL dependency script is missing or not executable: $ensure_script" >&2
        exit 1
    fi

    "$ensure_script" "$@"
}

for component in "${SOURCE_DEPENDENCIES[@]}"; do
    ensure_component "$component" --force
done

if [[ "$PROFILE" == "rp" ]]; then
    ensure_component pico_sdk --enable
    ensure_component picotool --enable
fi

echo "JaszczurHAL dependencies are ready for the $PROFILE profile."
