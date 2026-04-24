#!/usr/bin/env python3
"""
Shared persistent serial monitor.

This is the canonical Fiesta serial monitor implementation used by the VS Code
Ctrl+Shift+3 workflow. Module-local serial-persistent.py and serial-monitor.*
files are thin wrappers around this script.

Behavior:
- waits for a serial device,
- connects and reads continuously,
- reconnects after unplug/disconnect,
- exits cleanly on Ctrl+C.

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
    print("pyserial is not installed.")
    print("Install it with: pip install pyserial --break-system-packages")
    sys.exit(1)

CYAN = "\033[0;36m"
GREEN = "\033[0;32m"
YELLOW = "\033[1;33m"
RED = "\033[0;31m"
DIM = "\033[2m"
BOLD = "\033[1m"
NC = "\033[0m"

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


def find_settings(project_dir=""):
    if project_dir:
        settings_path = os.path.join(project_dir, ".vscode", "settings.json")
    else:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        project_dir = os.path.dirname(script_dir)
        settings_path = os.path.join(project_dir, ".vscode", "settings.json")

    if os.path.isfile(settings_path):
        try:
            with open(settings_path, "r", encoding="utf-8") as handle:
                return json.load(handle)
        except Exception:
            return {}

    return {}


def get_preferred_port(cli_port, project_dir=""):
    if cli_port:
        return cli_port

    settings = find_settings(project_dir)
    candidates = [
        settings.get("persistentSerialMonitor.port", ""),
        settings.get("arduino.uploadPort", ""),
        settings.get("serial.port", ""),
    ]

    for port in candidates:
        if isinstance(port, str) and port.strip():
            return port.strip()

    return ""


def resolve_preferred_port(cli_port, project_dir=""):
    return get_preferred_port(cli_port, project_dir)


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


def wait_for_device(mode, cli_port="", project_dir=""):
    spinner = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]
    index = 0
    last_preferred = None

    while True:
        preferred_port = resolve_preferred_port(cli_port, project_dir)

        if preferred_port != last_preferred:
            print(f"\r{' ' * 140}\r", end="", flush=True)
            if last_preferred is not None:
                before = last_preferred if last_preferred else "auto"
                after = preferred_port if preferred_port else "auto"
                print(f"{CYAN}Port preference changed: {before} -> {after}{NC}")
            last_preferred = preferred_port

        port, reason = find_port(mode, preferred_port)

        if port:
            print(f"\r{' ' * 140}\r", end="", flush=True)
            time.sleep(0.3)
            if os.path.exists(port):
                print(f"{GREEN}Found port: {port} [{reason}]{NC}")
                return port

        ports = list_serial_ports()
        if ports:
            port_info = ", ".join(format_port_info(port) for port in ports)
            suffix = f" ({port_info})"
        else:
            fallback = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
            suffix = f" ({', '.join(fallback)})" if fallback else ""

        print(
            f"\r{YELLOW}Waiting for device [{mode}]... "
            f"{DIM}{spinner[index % len(spinner)]}{suffix}{NC}   ",
            end="",
            flush=True,
        )

        index += 1
        time.sleep(0.5)


def clear_hupcl(fd):
    """Clear HUPCL so the kernel does not drop DTR on close (Linux only)."""
    import fcntl
    import struct

    tcgets = 0x5401
    tcsets = 0x5402
    hupcl = 0x0400
    try:
        buf = bytearray(60)
        fcntl.ioctl(fd, tcgets, buf)
        cflag = struct.unpack_from("I", buf, 8)[0]
        cflag &= ~hupcl
        struct.pack_into("I", buf, 8, cflag)
        fcntl.ioctl(fd, tcsets, buf)
    except Exception:
        pass


def open_serial(port, baud, exclusive=True):
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 0.5
    ser.write_timeout = 0.5
    ser.xonxoff = False
    ser.dsrdtr = False
    ser.rtscts = False

    if exclusive:
        try:
            ser.exclusive = True
        except Exception:
            pass

    ser.open()
    clear_hupcl(ser.fd)

    try:
        ser.setRTS(False)
    except Exception:
        pass

    try:
        ser.setDTR(True)
    except Exception:
        pass

    time.sleep(0.2)

    try:
        ser.reset_input_buffer()
    except Exception:
        pass

    return ser


def is_exclusive_lock_error(exc):
    text = str(exc).lower()
    return (
        "could not exclusively lock port" in text
        or "resource temporarily unavailable" in text
        or "errno 11" in text
    )


def monitor(port, baud, cli_port="", project_dir="", use_exclusive=True):
    try:
        ser = open_serial(port, baud, exclusive=use_exclusive)
    except serial.SerialException as exc:
        if use_exclusive and is_exclusive_lock_error(exc):
            print(
                f"{YELLOW}Port {port} is already locked by another process; "
                f"retrying without exclusive lock...{NC}"
            )
            try:
                ser = open_serial(port, baud, exclusive=False)
            except serial.SerialException as fallback_exc:
                print(f"{RED}Cannot open {port}: {fallback_exc}{NC}")
                return "error"
        else:
            print(f"{RED}Cannot open {port}: {exc}{NC}")
            return "error"


    print(f"{GREEN}Connected to {port} @ {baud}{NC}")
    print(f"{DIM}{'─' * 80}{NC}")

    try:
        while True:
            try:
                raw = ser.readline()
            except (serial.SerialException, OSError):
                break

            preferred_port = resolve_preferred_port(cli_port, project_dir)
            if preferred_port and preferred_port != port:
                print()
                print(f"{CYAN}Port preference changed: {port} -> {preferred_port}{NC}")
                try:
                    ser.close()
                except Exception:
                    pass
                return "port-changed"

            if not raw:
                continue

            raw = raw.replace(b"\r\n", b"\n").replace(b"\r", b"\n")

            try:
                text = raw.decode("utf-8", errors="replace").rstrip("\n")
            except Exception:
                text = repr(raw)

            if text:
                print(text)

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
        "-b",
        "--baud",
        type=int,
        default=115200,
        help="Baud rate (default: 115200)",
    )
    parser.add_argument(
        "-m",
        "--mode",
        choices=["pico", "probe", "any"],
        default="pico",
        help="Autodetection mode (default: pico)",
    )
    parser.add_argument(
        "--project-dir",
        default="",
        help="Project directory used to read .vscode/settings.json for preferred port lookup",
    )
    parser.add_argument(
        "--no-exclusive",
        action="store_true",
        help="Disable exclusive lock attempt when opening serial port",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    preferred = resolve_preferred_port(args.port, args.project_dir)

    print()
    print(f"{BOLD}{CYAN}╔══════════════════════════════════════════════════════════╗{NC}")
    print(f"{BOLD}{CYAN}║              Persistent Serial Monitor                  ║{NC}")
    print(f"{BOLD}{CYAN}╚══════════════════════════════════════════════════════════╝{NC}")
    print(f"  Baud:   {GREEN}{args.baud}{NC}")
    print(f"  Mode:   {GREEN}{args.mode}{NC}")
    print(f"  Port:   {GREEN}{preferred if preferred else 'auto'}{NC}")
    print(f"  Lock:   {GREEN}{'shared' if args.no_exclusive else 'exclusive+fallback'}{NC}")
    print(f"  {YELLOW}Ctrl+C{NC} to stop")
    print()

    while True:
        preferred = resolve_preferred_port(args.port, args.project_dir)
        port, _ = find_port(args.mode, preferred)

        if not port:
            port = wait_for_device(args.mode, args.port, args.project_dir)

        result = monitor(
            port,
            args.baud,
            args.port,
            args.project_dir,
            use_exclusive=not args.no_exclusive,
        )

        if result == "quit":
            print(f"\n{CYAN}Done.{NC}")
            break

        if result == "port-changed":
            print(f"\n{DIM}{'─' * 80}{NC}")
            time.sleep(0.2)
            continue

        if result == "disconnected":
            print(f"\n{DIM}{'─' * 80}{NC}")
            print(f"{YELLOW}Device disconnected: {port}{NC}\n")
            time.sleep(0.5)
            continue

        if result == "error":
            time.sleep(2.0)


if __name__ == "__main__":
    signal.signal(signal.SIGINT, lambda s, f: (print(f"\n{CYAN}Done.{NC}"), sys.exit(0)))
    main()
