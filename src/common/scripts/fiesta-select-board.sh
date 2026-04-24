#!/usr/bin/env bash
# =============================================================================
# Shared target board selector
# Updates .vscode/settings.json with selected FQBN and board options
# =============================================================================
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# Format: "ID|FQBN|Description|Chip"
BOARDS=(
    "1|rp2040:rp2040:rpipico|Raspberry Pi Pico|RP2040"
    "2|rp2040:rp2040:rpipicow|Raspberry Pi Pico W|RP2040"
    "3|rp2040:rp2040:rpipico2|Raspberry Pi Pico 2|RP2350"
    "4|rp2040:rp2040:rpipico2w|Raspberry Pi Pico 2 W|RP2350"
    "5|rp2040:rp2040:waveshare_rp2040_zero|Waveshare RP2040-Zero|RP2040"
    "6|rp2040:rp2040:waveshare_rp2040_plus|Waveshare RP2040-Plus|RP2040"
)

usage() {
    echo -e "${RED}Usage: $0 <project-dir>${NC}" >&2
    exit 2
}

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

    if [[ -n "$options" ]]; then
        echo "${fqbn}:${options}"
    else
        echo "$fqbn"
    fi
}

update_settings() {
    local settings_file="$1"
    local fqbn="$2"
    local board_desc="$3"

    mkdir -p "$(dirname "$settings_file")"

    if [[ -f "$settings_file" ]]; then
        FQBN_VAL="$fqbn" DESC_VAL="$board_desc" SFILE="$settings_file" \
        python3 << 'PYEOF'
import json
import os

settings_file = os.environ["SFILE"]
fqbn = os.environ["FQBN_VAL"]
desc = os.environ["DESC_VAL"]

with open(settings_file, "r") as handle:
    settings = json.load(handle)

settings["arduino.fqbn"] = fqbn
settings["arduino.boardDescription"] = desc

with open(settings_file, "w") as handle:
    json.dump(settings, handle, indent=4)
    handle.write("\n")
PYEOF
    else
        echo "  settings.json does not exist. Run this script from your project directory."
        echo "  Expected path: $settings_file"
        exit 1
    fi

    echo ""
    echo -e "  ${GREEN}✓${NC} Board set: ${BOLD}${board_desc}${NC}"
    echo -e "  ${GREEN}✓${NC} FQBN: ${fqbn}"
    echo ""
    echo -e "  ${YELLOW}Next step:${NC} run ${CYAN}./scripts/refresh-intellisense.sh${NC}"
    echo "  to refresh IntelliSense configuration for the new board."
}

main() {
    [[ $# -ge 1 ]] || usage

    local project_dir="$1"
    local settings_file="$project_dir/.vscode/settings.json"
    local choice custom_fqbn custom_desc full_fqbn

    show_menu
    read -rp "  Select board: " choice

    case "$choice" in
        [1-6])
            for board in "${BOARDS[@]}"; do
                IFS='|' read -r id fqbn desc chip <<< "$board"
                if [[ "$id" == "$choice" ]]; then
                    full_fqbn=$(ask_advanced_options "$fqbn" "$chip")
                    update_settings "$settings_file" "$full_fqbn" "$desc"
                    break
                fi
            done
            ;;
        c|C)
            echo ""
            read -rp "  Enter full FQBN (e.g. rp2040:rp2040:rpipico): " custom_fqbn
            read -rp "  Enter board description: " custom_desc
            update_settings "$settings_file" "$custom_fqbn" "$custom_desc"
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