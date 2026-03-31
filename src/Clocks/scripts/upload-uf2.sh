#!/usr/bin/env bash
# =============================================================================
# Upload przez BOOTSEL (UF2)
# Kompiluje projekt i kopiuje .uf2 na zamontowany dysk BOOTSEL
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SETTINGS_FILE="$PROJECT_DIR/.vscode/settings.json"
BUILD_DIR="$PROJECT_DIR/.build"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${CYAN}[INFO]${NC} $*"; }
ok()    { echo -e "${GREEN}[OK]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
err()   { echo -e "${RED}[ERROR]${NC} $*"; }

# Wczytaj ustawienia
read_setting() {
    python3 -c "
import json
with open('$SETTINGS_FILE') as f:
    s = json.load(f)
print(s.get('$1', '${2:-}'))
" 2>/dev/null
}

CLI=$(read_setting "arduino.cliPath" "arduino-cli")
FQBN=$(read_setting "arduino.fqbn")
SKETCHBOOK=$(read_setting "arduino.sketchbookPath")

LIB_ARGS=""
if [[ -n "$SKETCHBOOK" && -d "$SKETCHBOOK/libraries" ]]; then
    LIB_ARGS="--libraries $SKETCHBOOK/libraries"
fi

# Kompilacja
info "Kompilacja..."
info "  FQBN: $FQBN"
if ! $CLI compile --fqbn "$FQBN" --build-path "$BUILD_DIR" $LIB_ARGS \
    --build-property "compiler.cpp.extra_flags=-I '$PROJECT_DIR'" \
    --build-property "compiler.c.extra_flags=-I '$PROJECT_DIR'" \
    "$PROJECT_DIR"; then
    err "Kompilacja nie powiodła się"
    exit 1
fi

# Szukaj pliku UF2
echo ""
info "Szukam pliku UF2..."
UF2=$(find "$BUILD_DIR" -name '*.uf2' -type f | head -1)

if [[ -z "$UF2" ]]; then
    err "Nie znaleziono pliku .uf2 w $BUILD_DIR"
    exit 1
fi

ok "Znaleziono: $UF2"

# Szukaj dysku BOOTSEL
info "Szukam dysku BOOTSEL..."
MOUNT=""
for name in RPI-RP2 RP2350 RPI-RP2350; do
    MOUNT=$(find /media/"$USER" -maxdepth 1 -name "$name" -type d 2>/dev/null | head -1)
    if [[ -n "$MOUNT" ]]; then
        break
    fi
done

if [[ -z "$MOUNT" ]]; then
    # Sprawdź też /run/media (niektóre distro)
    for name in RPI-RP2 RP2350 RPI-RP2350; do
        MOUNT=$(find /run/media/"$USER" -maxdepth 1 -name "$name" -type d 2>/dev/null | head -1)
        if [[ -n "$MOUNT" ]]; then
            break
        fi
    done
fi

if [[ -z "$MOUNT" ]]; then
    err "Nie znaleziono dysku BOOTSEL"
    echo ""
    echo "  Instrukcja:"
    echo "  1. Odłącz płytkę od USB"
    echo "  2. Przytrzymaj przycisk BOOTSEL"
    echo "  3. Podłącz USB (trzymając BOOTSEL)"
    echo "  4. Puść BOOTSEL — powinien pojawić się dysk RPI-RP2"
    echo "  5. Uruchom ten skrypt ponownie"
    echo ""
    echo "  Podłączone dyski w /media/$USER/:"
    ls /media/"$USER"/ 2>/dev/null || echo "    (brak)"
    exit 1
fi

# Kopiuj UF2
info "Kopiuję na $MOUNT..."
cp "$UF2" "$MOUNT/"
sync

echo ""
ok "Upload UF2 zakończony!"
ok "Plik: $(basename "$UF2") → $MOUNT/"
