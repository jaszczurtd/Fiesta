#!/usr/bin/env python3
"""
Persistent Serial Monitor.

Waits for a serial device, connects, reads continuously, and when the device
disappears it goes back to waiting. Ctrl+C exits.

Supported modes:
- pico  : only Pico USB CDC devices (RP2040 / RP2350), Debug Probe excluded
- probe : only Raspberry Pi Debug Probe / Picoprobe
- any   : any serial port (/dev/ttyACM* or /dev/ttyUSB*)

If a port is provided explicitly, the script will use that exact port without
VID:PID filtering.
"""

import argparse
import atexit
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

INSTANCE_LOCK_FD = None


class PortBusyError(serial.SerialException):
    """Raised when serial port is locked by another process."""


def _read_lock_holder_pid(fd):
    try:
        with os.fdopen(os.dup(fd), "r", encoding="utf-8", errors="ignore") as handle:
            value = handle.read().strip()
    except Exception:
        return None

    if value and value.isdigit():
        return int(value)

    return None


def _write_lock_holder_pid(fd):
    os.ftruncate(fd, 0)
    os.write(fd, f"{os.getpid()}\n".encode("utf-8"))
    os.fsync(fd)


def usb_id_str(vid, pid):
    if vid is None or pid is None:
        return None
    return f"{vid:04x}:{pid:04x}"


def acquire_instance_lock(mode, replace_existing=False):
    global INSTANCE_LOCK_FD

    lock_path = f"/tmp/reseter-serial-persistent-{mode}.lock"
    fd = os.open(lock_path, os.O_RDWR | os.O_CREAT, 0o644)

    import fcntl

    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        holder_pid = _read_lock_holder_pid(fd)

        if not replace_existing:
            holder_text = str(holder_pid) if holder_pid is not None else "unknown"
            os.close(fd)
            print(
                f"{YELLOW}[WARN] Another persistent monitor (mode={mode}) is already running"
                f" [pid={holder_text}]. Exiting duplicate instance.{NC}"
            )
            return False

        if holder_pid is not None and holder_pid != os.getpid():
            try:
                os.kill(holder_pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            except PermissionError:
                os.close(fd)
                print(
                    f"{RED}[ERROR] Cannot replace monitor pid={holder_pid} (permission denied).{NC}"
                )
                return False

        locked = False
        for _ in range(40):
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                locked = True
                break
            except BlockingIOError:
                time.sleep(0.1)

        if not locked:
            holder_text = str(holder_pid) if holder_pid is not None else "unknown"
            os.close(fd)
            print(
                f"{RED}[ERROR] Failed to take over monitor lock (mode={mode}, holder={holder_text}).{NC}"
            )
            return False

    _write_lock_holder_pid(fd)
    INSTANCE_LOCK_FD = fd
    return True


def release_instance_lock():
    global INSTANCE_LOCK_FD
    if INSTANCE_LOCK_FD is None:
        return

    try:
        os.close(INSTANCE_LOCK_FD)
    except Exception:
        pass
    INSTANCE_LOCK_FD = None


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

    # Heuristic fallback for boards exposing non-Raspberry VID:PID pairs.
    text_parts = [
        getattr(port_info, "description", ""),
        getattr(port_info, "manufacturer", ""),
        getattr(port_info, "product", ""),
        getattr(port_info, "interface", ""),
        getattr(port_info, "hwid", ""),
    ]
    text_blob = " ".join(part for part in text_parts if part).lower()

    if "debug probe" in text_blob or "picoprobe" in text_blob or "cmsis-dap" in text_blob:
        return "probe"

    pico_keywords = (
        "raspberry pi pico",
        "pico 2",
        "pico2",
        "rp2040",
        "rp2350",
    )
    if any(keyword in text_blob for keyword in pico_keywords):
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


def classify_device(device_path):
    for port in list_serial_ports():
        if port.device == device_path:
            return classify_port(port)
    return "other"


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
        return cli_port, True

    settings = find_settings()
    candidates = [
        settings.get("persistentSerialMonitor.port", ""),
        settings.get("arduino.uploadPort", ""),
        settings.get("serial.port", ""),
    ]

    for port in candidates:
        if isinstance(port, str) and port.strip():
            return port.strip(), False

    return "", False


def find_port(mode, preferred_port="", strict_preferred=False):
    if preferred_port:
        if os.path.exists(preferred_port):
            if strict_preferred:
                return preferred_port, f"preferred:{preferred_port}"

            preferred_kind = classify_device(preferred_port)
            if mode == "any" or preferred_kind == mode:
                return preferred_port, f"preferred:{preferred_port}"
        if strict_preferred:
            return None, f"preferred-missing:{preferred_port}"

    ports = list_serial_ports()
    for port in ports:
        if port_matches_mode(port, mode):
            return port.device, format_port_info(port)

    if preferred_port:
        return None, f"preferred-missing:{preferred_port};not-found"
    return None, "not-found"


def wait_for_device(mode, preferred_port="", strict_preferred=False):
    spinner = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]
    i = 0

    while True:
        port, reason = find_port(mode, preferred_port, strict_preferred)

        if port:
            print(f"\r{' ' * 140}\r", end="", flush=True)
            time.sleep(0.3)
            if os.path.exists(port):
                print(f"{GREEN}Found port: {port} [{reason}]{NC}")
                return port

        ports = list_serial_ports()
        if ports:
            port_info = ", ".join(format_port_info(p) for p in ports)
            suffix = f" ({port_info})"
        else:
            fallback = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
            suffix = f" ({', '.join(fallback)})" if fallback else ""

        print(
            f"\r{YELLOW}Waiting for device [{mode}]... "
            f"{DIM}{spinner[i % len(spinner)]}{suffix}{NC}   ",
            end="",
            flush=True,
        )

        i += 1
        time.sleep(0.5)


def _clear_hupcl(fd: int) -> None:
    """Clear HUPCL so the kernel does not drop DTR on close (Linux only).

    Without this, closing the serial port lowers DTR, which causes the
    RP2040 USB CDC to reset and re-enumerate — triggering a spurious
    disconnect/reconnect cycle in the monitor.
    """
    import fcntl
    import struct

    TCGETS = 0x5401
    TCSETS = 0x5402
    HUPCL  = 0x0400
    try:
        buf = bytearray(60)
        fcntl.ioctl(fd, TCGETS, buf)
        cflag = struct.unpack_from("I", buf, 8)[0]
        cflag &= ~HUPCL
        struct.pack_into("I", buf, 8, cflag)
        fcntl.ioctl(fd, TCSETS, buf)
    except Exception:
        pass


def open_serial(port, baud):
    ser = serial.Serial()
    ser.port         = port
    ser.baudrate     = baud
    ser.timeout      = 0.5
    ser.write_timeout = 0.5
    ser.xonxoff      = False
    ser.dsrdtr       = False
    ser.rtscts       = False

    try:
        ser.exclusive = True
    except Exception:
        pass

    try:
        ser.open()
    except serial.SerialException as err:
        message = str(err).lower()
        lock_conflict = "exclusively lock port" in message or "resource temporarily unavailable" in message

        if lock_conflict:
            raise PortBusyError(str(err)) from err

        raise
    _clear_hupcl(ser.fd)

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


def monitor(port, baud):
    try:
        ser = open_serial(port, baud)
    except PortBusyError as e:
        print(f"{YELLOW}Port busy ({port}): {e}{NC}")
        return "busy"
    except serial.SerialException as e:
        print(f"{RED}Cannot open {port}: {e}{NC}")
        return "error"

    print(f"{GREEN}Connected to {port} @ {baud}{NC}")
    print(f"{DIM}{'─' * 80}{NC}")

    try:
        while True:
            try:
                raw = ser.readline()
            except (serial.SerialException, OSError):
                break

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
    parser.add_argument(
        "--replace-existing",
        action="store_true",
        help="Replace running monitor process in the same mode",
    )
    return parser.parse_args()


def main():
    args = parse_args()

    if not acquire_instance_lock(args.mode, args.replace_existing):
        return
    atexit.register(release_instance_lock)

    preferred, strict_preferred = get_preferred_port(args.port)

    print()
    print(f"{BOLD}{CYAN}╔══════════════════════════════════════════════════════════╗{NC}")
    print(f"{BOLD}{CYAN}║              Persistent Serial Monitor                  ║{NC}")
    print(f"{BOLD}{CYAN}╚══════════════════════════════════════════════════════════╝{NC}")
    print(f"  Baud:   {GREEN}{args.baud}{NC}")
    print(f"  Mode:   {GREEN}{args.mode}{NC}")
    print(f"  Port:   {GREEN}{preferred if preferred else 'auto'}{NC}")
    print(f"  {YELLOW}Ctrl+C{NC} to stop")
    print()

    while True:
        preferred, strict_preferred = get_preferred_port(args.port)
        port, _ = find_port(args.mode, preferred, strict_preferred)

        if not port:
            port = wait_for_device(args.mode, preferred, strict_preferred)

        result = monitor(port, args.baud)

        if result == "quit":
            print(f"\n{CYAN}Done.{NC}")
            break

        if result == "disconnected":
            print(f"\n{DIM}{'─' * 80}{NC}")
            print(f"{YELLOW}Device disconnected: {port}{NC}\n")
            time.sleep(0.5)
            continue

        if result == "busy":
            time.sleep(1.0)
            continue

        if result == "error":
            time.sleep(2.0)


if __name__ == "__main__":
    signal.signal(signal.SIGINT, lambda s, f: (print(f"\n{CYAN}Done.{NC}"), sys.exit(0)))
    main()
