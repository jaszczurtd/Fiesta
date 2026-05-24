#!/usr/bin/env bash
# =============================================================================
# Serial Monitor
# Łączy się z portem szeregowym płytki
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SETTINGS_FILE="$PROJECT_DIR/.vscode/settings.json"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# Wczytaj ustawienia
read_setting() {
    python3 -c "
import json
with open('$SETTINGS_FILE') as f:
    s = json.load(f)
print(s.get('$1', '${2:-}'))
" 2>/dev/null
}

PORT=$(read_setting "arduino.uploadPort" "/dev/ttyACM0")
BAUD="115200"

echo -e "${CYAN}Serial Monitor${NC}"
echo -e "  Port: ${GREEN}$PORT${NC}"
echo -e "  Baud: ${GREEN}$BAUD${NC}"
echo ""

# Sprawdź czy port istnieje
if [[ ! -e "$PORT" ]]; then
    echo -e "${RED}Port $PORT nie istnieje${NC}"
    echo ""
    echo "Dostępne porty:"
    ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || echo "  (brak)"
    exit 1
fi

# Sprawdź czy port jest zajęty
if fuser "$PORT" &>/dev/null; then
    echo -e "${YELLOW}Port $PORT jest zajęty przez inny proces:${NC}"
    fuser -v "$PORT" 2>&1 || true
    echo ""
    read -rp "Zabić procesy blokujące port? [t/N] " kill_answer
    if [[ "$kill_answer" =~ ^[tTyY]$ ]]; then
        fuser -k "$PORT" 2>/dev/null || true
        sleep 1
    else
        exit 1
    fi
fi

# Wybierz narzędzie do połączenia
# Priorytet: cat (najprostsze), screen, minicom
echo -e "Łączenie... (${YELLOW}Ctrl+C${NC} aby zakończyć)"
echo "----------------------------------------"

# Konfiguruj port
stty -F "$PORT" "$BAUD" cs8 -cstopb -parenb raw -echo 2>/dev/null || true

# Trap Ctrl+C żeby posprzątać
cleanup() {
    echo ""
    echo -e "${CYAN}Rozłączono.${NC}"
    exit 0
}
trap cleanup INT TERM

# Czytaj port — cat działa najlepiej w VS Code terminalu
cat "$PORT"
