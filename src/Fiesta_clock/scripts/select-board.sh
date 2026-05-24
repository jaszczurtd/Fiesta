#!/usr/bin/env bash
# =============================================================================
# Target board selector
# Updates .vscode/settings.json with selected FQBN and board options
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SETTINGS_FILE="$PROJECT_DIR/.vscode/settings.json"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# ---------------------------------------------------------------------------
# Predefined boards
# Format: "ID|FQBN|Description|Chip"
# ---------------------------------------------------------------------------
BOARDS=(
    "1|rp2040:rp2040:rpipico|Raspberry Pi Pico|RP2040"
    "2|rp2040:rp2040:rpipicow|Raspberry Pi Pico W|RP2040"
    "3|rp2040:rp2040:rpipico2|Raspberry Pi Pico 2|RP2350"
    "4|rp2040:rp2040:rpipico2w|Raspberry Pi Pico 2 W|RP2350"
    "5|rp2040:rp2040:waveshare_rp2040_zero|Waveshare RP2040-Zero|RP2040"
    "6|rp2040:rp2040:waveshare_rp2040_plus|Waveshare RP2040-Plus|RP2040"
)

# ---------------------------------------------------------------------------
# Board menu options (from boards.txt)
# Key options that can be changed per project
# ---------------------------------------------------------------------------
# Default values matching boards.txt defaults
DEFAULT_FLASH=""       # empty = use board default
DEFAULT_FREQ=""        # empty = use board default (133MHz RP2040, 150MHz RP2350)
DEFAULT_OPT="Small"    # Small, Optimize, Optimize2, Optimize3, Debug
DEFAULT_DBGPORT="Disabled"  # Disabled, Serial, Serial1, Serial2
DEFAULT_DBGLVL="None"       # None, Core, SPI, Wire, All, NDEBUG
DEFAULT_USBSTACK="picosdk"  # picosdk, tinyusb, nousb
DEFAULT_IPSTACK="ipv4only"  # ipv4only, ipv4ipv6

# ---------------------------------------------------------------------------
# Show menu
# ---------------------------------------------------------------------------
show_menu() {
    echo ""
    echo -e "${BOLD}╔══════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BOLD}║                  Select Target Board                    ║${NC}"
    echo -e "${BOLD}╚══════════════════════════════════════════════════════════╝${NC}"
    echo ""

    for board in "${BOARDS[@]}"; do
        IFS='|' read -r id fqbn desc chip <<< "$board"
        printf "  ${CYAN}%s${NC}) %-35s ${YELLOW}[%s]${NC}\n" "$id" "$desc" "$chip"
    done

    echo ""
    echo -e "  ${CYAN}c${NC}) Custom configuration (enter FQBN manually)"
    echo -e "  ${CYAN}q${NC}) Quit"
    echo ""
}

# ---------------------------------------------------------------------------
# Prompt for advanced options
# ---------------------------------------------------------------------------
ask_advanced_options() {
    local fqbn="$1"
    local chip="$2"

    echo "" >&2
    read -rp "  Configure advanced options (flash/freq/USB/debug)? [y/N] " advanced

    if [[ ! "$advanced" =~ ^[yY]$ ]]; then
        echo "$fqbn"
        return
    fi

    local options=""

    # --- Flash Size ---
    echo "" >&2
    echo -e "  ${BOLD}Flash Size / Filesystem:${NC}" >&2
    if [[ "$chip" == "RP2040" ]]; then
        echo "    1) 2MB (no FS)  [Pico default]" >&2
        echo "    2) 2MB (64KB FS)" >&2
        echo "    3) 2MB (128KB FS)" >&2
        echo "    4) 2MB (256KB FS)" >&2
        echo "    5) 2MB (512KB FS)" >&2
    else
        echo "    1) 4MB (no FS)  [Pico 2 default]" >&2
        echo "    2) 4MB (2MB FS)" >&2
        echo "    3) 4MB (3MB FS)" >&2
    fi
    echo "    Enter) Keep default" >&2
    read -rp "    Choice: " flash_choice

    case "$flash_choice" in
        1) ;;
        2) [[ "$chip" == "RP2040" ]] && options="flash=2097152_65536" || options="flash=4194304_2097152" ;;
        3) [[ "$chip" == "RP2040" ]] && options="flash=2097152_131072" || options="flash=4194304_3145728" ;;
        4) [[ "$chip" == "RP2040" ]] && options="flash=2097152_262144" ;;
        5) [[ "$chip" == "RP2040" ]] && options="flash=2097152_524288" ;;
        *) ;;
    esac

    # --- CPU Frequency ---
    echo "" >&2
    echo -e "  ${BOLD}CPU Frequency:${NC}" >&2
    if [[ "$chip" == "RP2040" ]]; then
        echo "    1) 133 MHz [default]" >&2
        echo "    2) 50 MHz" >&2
        echo "    3) 125 MHz" >&2
        echo "    4) 150 MHz (overclock)" >&2
        echo "    5) 200 MHz (overclock)" >&2
        echo "    6) 250 MHz (overclock)" >&2
    else
        echo "    1) 150 MHz [default]" >&2
        echo "    2) 125 MHz" >&2
        echo "    3) 200 MHz (overclock)" >&2
        echo "    4) 250 MHz (overclock)" >&2
        echo "    5) 300 MHz (overclock)" >&2
    fi
    echo "    Enter) Keep default" >&2
    read -rp "    Choice: " freq_choice

    case "$freq_choice" in
        1) ;;
        2) [[ "$chip" == "RP2040" ]] && options="${options:+$options,}freq=50" || options="${options:+$options,}freq=125" ;;
        3) [[ "$chip" == "RP2040" ]] && options="${options:+$options,}freq=125" || options="${options:+$options,}freq=200" ;;
        4) [[ "$chip" == "RP2040" ]] && options="${options:+$options,}freq=150" || options="${options:+$options,}freq=250" ;;
        5) [[ "$chip" == "RP2040" ]] && options="${options:+$options,}freq=200" || options="${options:+$options,}freq=300" ;;
        6) [[ "$chip" == "RP2040" ]] && options="${options:+$options,}freq=250" ;;
        *) ;;
    esac

    # --- USB Stack ---
    echo "" >&2
    echo -e "  ${BOLD}USB Stack:${NC}" >&2
    echo "    1) Pico SDK USB [default]" >&2
    echo "    2) Adafruit TinyUSB" >&2
    echo "    3) No USB" >&2
    echo "    Enter) Keep default" >&2
    read -rp "    Choice: " usb_choice

    case "$usb_choice" in
        2) options="${options:+$options,}usbstack=tinyusb" ;;
        3) options="${options:+$options,}usbstack=nousb" ;;
        *) ;;
    esac

    # --- Debug Port ---
    echo "" >&2
    echo -e "  ${BOLD}Debug Port:${NC}" >&2
    echo "    1) Disabled [default]" >&2
    echo "    2) Serial (USB)" >&2
    echo "    3) Serial1 (UART0, pins 0/1)" >&2
    echo "    4) Serial2 (UART1, pins 4/5)" >&2
    echo "    Enter) Keep default" >&2
    read -rp "    Choice: " dbg_choice

    case "$dbg_choice" in
        2) options="${options:+$options,}dbgport=Serial" ;;
        3) options="${options:+$options,}dbgport=Serial1" ;;
        4) options="${options:+$options,}dbgport=Serial2" ;;
        *) ;;
    esac

    # --- Debug Level ---
    echo "" >&2
    echo -e "  ${BOLD}Debug Level:${NC}" >&2
    echo "    1) None [default]" >&2
    echo "    2) Core" >&2
    echo "    3) All" >&2
    echo "    Enter) Keep default" >&2
    read -rp "    Choice: " dbglvl_choice

    case "$dbglvl_choice" in
        2) options="${options:+$options,}dbglvl=Core" ;;
        3) options="${options:+$options,}dbglvl=All" ;;
        *) ;;
    esac

    # --- IP Stack (for WiFi boards) ---
    if [[ "$fqbn" == *"picow"* || "$fqbn" == *"pico2w"* ]]; then
        echo "" >&2
        echo -e "  ${BOLD}IP Stack:${NC}" >&2
        echo "    1) IPv4 only [default]" >&2
        echo "    2) IPv4 + IPv6" >&2
        echo "    Enter) Keep default" >&2
        read -rp "    Choice: " ip_choice

        case "$ip_choice" in
            2) options="${options:+$options,}ipstack=ipv4ipv6" ;;
            *) ;;
        esac
    fi

    # Print ONLY the resulting FQBN to stdout
    if [[ -n "$options" ]]; then
        echo "${fqbn}:${options}"
    else
        echo "$fqbn"
    fi
}

# ---------------------------------------------------------------------------
# Update settings.json
# ---------------------------------------------------------------------------
update_settings() {
    local fqbn="$1"
    local board_desc="$2"

    mkdir -p "$(dirname "$SETTINGS_FILE")"

    if [[ -f "$SETTINGS_FILE" ]]; then
        # Pass values via environment variables for safe quoting
        FQBN_VAL="$fqbn" DESC_VAL="$board_desc" SFILE="$SETTINGS_FILE" \
        python3 << 'PYEOF'
import json, os

settings_file = os.environ["SFILE"]
fqbn = os.environ["FQBN_VAL"]
desc = os.environ["DESC_VAL"]

with open(settings_file, "r") as f:
    settings = json.load(f)

settings["arduino.fqbn"] = fqbn
settings["arduino.boardDescription"] = desc

with open(settings_file, "w") as f:
    json.dump(settings, f, indent=4)
    f.write("\n")

print("OK")
PYEOF
    else
        echo "  settings.json does not exist. Run this script from your project directory."
        echo "  Expected path: $SETTINGS_FILE"
        exit 1
    fi

    echo ""
    echo -e "  ${GREEN}✓${NC} Board set: ${BOLD}${board_desc}${NC}"
    echo -e "  ${GREEN}✓${NC} FQBN: ${fqbn}"
    echo ""
    echo -e "  ${YELLOW}Next step:${NC} run ${CYAN}./scripts/refresh-intellisense.sh${NC}"
    echo "  to refresh IntelliSense configuration for the new board."
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    show_menu
    read -rp "  Select board: " choice

    case "$choice" in
        [1-6])
            for board in "${BOARDS[@]}"; do
                IFS='|' read -r id fqbn desc chip <<< "$board"
                if [[ "$id" == "$choice" ]]; then
                    local full_fqbn
                    full_fqbn=$(ask_advanced_options "$fqbn" "$chip")
                    update_settings "$full_fqbn" "$desc"
                    break
                fi
            done
            ;;
        c|C)
            echo ""
            read -rp "  Enter full FQBN (e.g. rp2040:rp2040:rpipico): " custom_fqbn
            read -rp "  Enter board description: " custom_desc
            update_settings "$custom_fqbn" "$custom_desc"
            ;;
        q|Q)
            echo "  Cancelled."
            exit 0
            ;;
        *)
            echo -e "  ${RED}Invalid selection${NC}"
            exit 1
            ;;
    esac
}

main "$@"
