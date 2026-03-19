#!/usr/bin/env python3
"""
Persistent Serial Monitor.

Waits for a serial device, connects, reads continuously, and when the device
disappears it goes back to waiting. Ctrl+C exits.

Supported modes:
- pico  : only Pico/RP2040 USB CDC devices, Debug Probe excluded
- probe : only Raspberry Pi Debug Probe / Picoprobe
- any   : any serial port (/dev/ttyACM* or /dev/ttyUSB*)

If a port is provided explicitly, the script will use that exact port without
VID:PID filtering.
"""

import argparse
import glob
import json
import os
import signal
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("Brak pyserial.")
    print("Zainstaluj: pip install pyserial --break-system-packages")
    sys.exit(1)

CYAN   = "\033[0;36m"
GREEN  = "\033[0;32m"
YELLOW = "\033[1;33m"
RED    = "\033[0;31m"
DIM    = "\033[2m"
BOLD   = "\033[1m"
NC     = "\033[0m"

PICO_USB_IDS = {
    "2e8a:000a",  # Pico
    "2e8a:f00a",  # Pico W
    "2e8a:000f",  # Pico 2
    "2e8a:f00f",  # Pico 2 W
    "2e8a:0003",  # RP2040-Zero / RP2040-Plus and similar
    "2e8a:1020",  # RP2040-Plus alternate
    "2e8a:103a",  # RP2040 One / Matrix
}

DEBUG_PROBE_IDS = {
    "2e8a:000c",  # Raspberry Pi Debug Probe
    "2e8a:0004",  # Picoprobe older firmware
}


def usb_id_str(vid, pid):
    if vid is None or pid is None:
        return None
    return f"{vid:04x}:{pid:04x}"


def is_serial_candidate(device):
    return device.startswith("/dev/ttyACM") or device.startswith("/dev/ttyUSB")


def list_serial_ports():
    ports = []
    for port in list_ports.comports():
        if not is_serial_candidate(port.device):
            continue
        ports.append(port)
    ports.sort(key=lambda p: p.device)
    return ports


def classify_port(port_info):
    uid = usb_id_str(port_info.vid, port_info.pid)

    if uid in DEBUG_PROBE_IDS:
        return "probe"

    if uid in PICO_USB_IDS:
        return "pico"

    if uid is not None and uid.startswith("2e8a:"):
        return "pico"

    return "other"


def port_matches_mode(port_info, mode):
    kind = classify_port(port_info)

    if mode == "any":
        return True
    if mode == "pico":
        return kind == "pico"
    if mode == "probe":
        return kind == "probe"

    return False


def format_port_info(port_info):
    uid = usb_id_str(port_info.vid, port_info.pid) or "?:????"
    kind = classify_port(port_info)
    desc = port_info.description or "no-description"
    return f"{port_info.device}[{kind}|{uid}|{desc}]"


def find_settings():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    settings_path = os.path.join(project_dir, ".vscode", "settings.json")

    if os.path.isfile(settings_path):
        try:
            with open(settings_path, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            return {}

    return {}


def get_preferred_port(cli_port):
    if cli_port:
        return cli_port

    settings = find_settings()
    candidates = [
        settings.get("persistentSerialMonitor.port", ""),
        settings.get("arduino.uploadPort", ""),
        settings.get("serial.port", ""),
    ]

    for port in candidates:
        if isinstance(port, str) and port.strip():
            return port.strip()

    return ""


def find_port(mode, preferred_port=""):
    if preferred_port:
        if os.path.exists(preferred_port):
            return preferred_port, f"preferred:{preferred_port}"
        return None, f"preferred-missing:{preferred_port}"

    ports = list_serial_ports()
    for port in ports:
        if port_matches_mode(port, mode):
            return port.device, format_port_info(port)

    return None, "not-found"


def wait_for_device(mode, preferred_port=""):
    spinner = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]
    i = 0

    while True:
        port, reason = find_port(mode, preferred_port)

        if port:
            print(f"\r{' ' * 120}\r", end="", flush=True)
            time.sleep(1.0)
            if os.path.exists(port):
                print(f"{GREEN}Znaleziono port: {port} [{reason}]{NC}")
                return port

        ports = list_serial_ports()
        if ports:
            port_info = ", ".join(format_port_info(p) for p in ports)
            suffix = f" ({port_info})"
        else:
            fallback = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
            suffix = f" ({', '.join(fallback)})" if fallback else ""

        print(
            f"\r{YELLOW}Czekam na urządzenie [{mode}]... "
            f"{DIM}{spinner[i % len(spinner)]}{suffix}{NC}   ",
            end="",
            flush=True,
        )

        i += 1
        time.sleep(0.5)


def open_serial(port, baud):
    return serial.Serial(
        port=port,
        baudrate=baud,
        timeout=0.5,
        dsrdtr=False,
        rtscts=False,
        xonxoff=False,
    )


def monitor(port, baud):
    try:
        ser = open_serial(port, baud)
    except serial.SerialException as e:
        print(f"{RED}Nie mogę otworzyć {port}: {e}{NC}")
        return "error"

    print(f"{GREEN}Połączono z {port} @ {baud}{NC}")
    print(f"{DIM}{'─' * 80}{NC}")

    try:
        while True:
            if not os.path.exists(port):
                break

            try:
                waiting = ser.in_waiting
                data = ser.read(waiting if waiting > 0 else 1)

                if data:
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()

            except (serial.SerialException, OSError):
                break

    except KeyboardInterrupt:
        try:
            ser.close()
        except Exception:
            pass
        return "quit"

    try:
        ser.close()
    except Exception:
        pass

    return "disconnected"


def parse_args():
    parser = argparse.ArgumentParser(
        description="Persistent serial monitor for Pico / Debug Probe / any serial device."
    )
    parser.add_argument(
        "port",
        nargs="?",
        default="",
        help="Explicit serial port, e.g. /dev/ttyACM0 or /dev/ttyUSB0",
    )
    parser.add_argument(
        "-b", "--baud",
        type=int,
        default=115200,
        help="Baud rate (default: 115200)",
    )
    parser.add_argument(
        "-m", "--mode",
        choices=["pico", "probe", "any"],
        default="pico",
        help="Autodetection mode (default: pico)",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    preferred = get_preferred_port(args.port)

    print()
    print(f"{BOLD}{CYAN}╔══════════════════════════════════════════════════════════╗{NC}")
    print(f"{BOLD}{CYAN}║              Persistent Serial Monitor                  ║{NC}")
    print(f"{BOLD}{CYAN}╚══════════════════════════════════════════════════════════╝{NC}")
    print(f"  Baud:   {GREEN}{args.baud}{NC}")
    print(f"  Mode:   {GREEN}{args.mode}{NC}")
    print(f"  Port:   {GREEN}{preferred if preferred else 'auto'}{NC}")
    print(f"  {YELLOW}Ctrl+C{NC} aby zakończyć")
    print()

    while True:
        port, _ = find_port(args.mode, preferred)

        if not port:
            port = wait_for_device(args.mode, preferred)

        result = monitor(port, args.baud)

        if result == "quit":
            print(f"\n{CYAN}Zakończono.{NC}")
            break

        if result == "disconnected":
            print(f"\n{DIM}{'─' * 80}{NC}")
            print(f"{YELLOW}Urządzenie odłączone: {port}{NC}\n")
            time.sleep(1.0)
            continue

        if result == "error":
            time.sleep(2.0)


if __name__ == "__main__":
    signal.signal(signal.SIGINT, lambda s, f: (print(f"\n{CYAN}Zakończono.{NC}"), sys.exit(0)))
    main()
