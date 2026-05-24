#!/usr/bin/env python3
"""Auto-detect Raspberry Pi Pico board variant and serial port.

Updates .vscode/settings.json fields:
- arduino.fqbn
- arduino.boardDescription
- arduino.uploadPort (when a serial Pico port is present)
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any

PID_TO_BOARD = {
    # Pico W (RP2040)
    "0xf00a": ("rp2040:rp2040:rpipicow", "Raspberry Pi Pico W (RP2040)"),
    "0xf10a": ("rp2040:rp2040:rpipicow", "Raspberry Pi Pico W (RP2040)"),
    # Pico 2 W (RP2350)
    "0xf00f": ("rp2040:rp2040:rpipico2w", "Raspberry Pi Pico 2 W (RP2350)"),
    "0xf10f": ("rp2040:rp2040:rpipico2w", "Raspberry Pi Pico 2 W (RP2350)"),
    # Pico (RP2040)
    "0x000a": ("rp2040:rp2040:rpipico", "Raspberry Pi Pico (RP2040)"),
    "0x010a": ("rp2040:rp2040:rpipico", "Raspberry Pi Pico (RP2040)"),
    "0x400a": ("rp2040:rp2040:rpipico", "Raspberry Pi Pico (RP2040)"),
    "0x410a": ("rp2040:rp2040:rpipico", "Raspberry Pi Pico (RP2040)"),
    "0x800a": ("rp2040:rp2040:rpipico", "Raspberry Pi Pico (RP2040)"),
    "0x810a": ("rp2040:rp2040:rpipico", "Raspberry Pi Pico (RP2040)"),
    "0xc00a": ("rp2040:rp2040:rpipico", "Raspberry Pi Pico (RP2040)"),
    "0xc10a": ("rp2040:rp2040:rpipico", "Raspberry Pi Pico (RP2040)"),
    # Pico 2 (RP2350)
    "0x000f": ("rp2040:rp2040:rpipico2", "Raspberry Pi Pico 2 (RP2350)"),
    "0x010f": ("rp2040:rp2040:rpipico2", "Raspberry Pi Pico 2 (RP2350)"),
    "0x400f": ("rp2040:rp2040:rpipico2", "Raspberry Pi Pico 2 (RP2350)"),
    "0x410f": ("rp2040:rp2040:rpipico2", "Raspberry Pi Pico 2 (RP2350)"),
    "0x800f": ("rp2040:rp2040:rpipico2", "Raspberry Pi Pico 2 (RP2350)"),
    "0x810f": ("rp2040:rp2040:rpipico2", "Raspberry Pi Pico 2 (RP2350)"),
    "0xc00f": ("rp2040:rp2040:rpipico2", "Raspberry Pi Pico 2 (RP2350)"),
    "0xc10f": ("rp2040:rp2040:rpipico2", "Raspberry Pi Pico 2 (RP2350)"),
}


def normalize_hex(value: str | None) -> str:
    if not value:
        return ""
    v = str(value).strip().lower()
    if not v:
        return ""
    if not v.startswith("0x"):
        v = f"0x{v}"
    suffix = v[2:]
    if not suffix:
        return ""
    try:
        num = int(suffix, 16)
    except ValueError:
        return ""
    return f"0x{num:04x}"


def to_bool(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        return value.strip().lower() in {"1", "true", "yes", "on"}
    return bool(value)


def run_json_command(cmd: list[str]) -> dict[str, Any]:
    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "command failed")
    return json.loads(result.stdout)


def infer_pid_from_dmesg() -> str:
    result = subprocess.run(["dmesg"], capture_output=True, text=True, check=False)
    if result.returncode != 0 or not result.stdout:
        return ""

    pattern = re.compile(r"idVendor=([0-9a-fA-F]{4}),\s*idProduct=([0-9a-fA-F]{4})")
    for line in reversed(result.stdout.splitlines()):
        match = pattern.search(line)
        if not match:
            continue
        vid = normalize_hex(match.group(1))
        pid = normalize_hex(match.group(2))
        if vid != "0x2e8a":
            continue
        if pid == "0x0003":
            # RP2 BOOTSEL mass storage ID. It doesn't identify model variant.
            continue
        if pid in PID_TO_BOARD:
            return pid
    return ""


def choose_serial_pico_port(detected_ports: list[dict[str, Any]]) -> tuple[str, str]:
    candidates: list[tuple[int, str, str]] = []

    for entry in detected_ports:
        port = entry.get("port", {})
        protocol = str(port.get("protocol", "")).lower()
        if protocol != "serial":
            continue

        address = str(port.get("address", ""))
        props = port.get("properties", {})
        vid = normalize_hex(props.get("vid"))
        pid = normalize_hex(props.get("pid"))

        if vid != "0x2e8a" or pid not in PID_TO_BOARD:
            continue

        # Prefer USB CDC ACM ports over other serial nodes.
        priority = 0
        if address.startswith("/dev/ttyacm"):
            priority = 2
        elif address.startswith("/dev/ttyusb"):
            priority = 1

        candidates.append((priority, address, pid))

    if not candidates:
        return "", ""

    candidates.sort(reverse=True)
    _, address, pid = candidates[0]
    return address, pid


def load_settings(settings_path: Path) -> dict[str, Any]:
    with settings_path.open("r", encoding="utf-8") as f:
        return json.load(f)


def save_settings(settings_path: Path, data: dict[str, Any]) -> None:
    with settings_path.open("w", encoding="utf-8") as f:
        json.dump(data, f, indent=4)
        f.write("\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Detect Pico board/port and update settings.json")
    parser.add_argument("--settings", required=True, help="Path to .vscode/settings.json")
    parser.add_argument("--cli", default="arduino-cli", help="arduino-cli executable")
    parser.add_argument("--quiet", action="store_true", help="Reduce output")
    args = parser.parse_args()

    settings_path = Path(args.settings)
    if not settings_path.exists():
        print(f"[WARN] Missing settings file: {settings_path}")
        return 0

    try:
        board_list = run_json_command([args.cli, "board", "list", "--format", "json"])
    except Exception as exc:
        if not args.quiet:
            print(f"[WARN] Auto-detect skipped: {exc}")
        return 0

    detected_ports = board_list.get("detected_ports", [])

    settings = load_settings(settings_path)
    changed = False
    lock_fqbn = to_bool(settings.get("arduino.lockFqbn", False))

    serial_port, runtime_pid = choose_serial_pico_port(detected_ports)
    uf2_present = any(str(p.get("port", {}).get("address", "")) == "UF2_Board" for p in detected_ports)

    selected_pid = runtime_pid
    source = "serial"

    if not selected_pid and uf2_present:
        selected_pid = infer_pid_from_dmesg()
        source = "uf2+dmesg"

    if selected_pid and selected_pid in PID_TO_BOARD:
        fqbn, description = PID_TO_BOARD[selected_pid]

        if not lock_fqbn:
            if settings.get("arduino.fqbn") != fqbn:
                settings["arduino.fqbn"] = fqbn
                changed = True
            if settings.get("arduino.boardDescription") != description:
                settings["arduino.boardDescription"] = description
                changed = True
        else:
            fqbn = str(settings.get("arduino.fqbn") or fqbn)
            description = str(settings.get("arduino.boardDescription") or description)

        if serial_port and settings.get("arduino.uploadPort") != serial_port:
            settings["arduino.uploadPort"] = serial_port
            changed = True

        if serial_port and settings.get("persistentSerialMonitor.port") != serial_port:
            settings["persistentSerialMonitor.port"] = serial_port
            changed = True

        if not args.quiet:
            port_msg = serial_port if serial_port else "(no serial port, UF2 mode)"
            if lock_fqbn:
                print(
                    f"[INFO] Auto-detect: board lock active, kept {description}, "
                    f"FQBN={fqbn}, port={port_msg}, source={source}"
                )
            else:
                print(f"[INFO] Auto-detect: {description}, FQBN={fqbn}, port={port_msg}, source={source}")
    elif not args.quiet:
        print("[WARN] Auto-detect: no supported Pico variant found")

    if changed:
        save_settings(settings_path, settings)
        if not args.quiet:
            print(f"[OK] Updated {settings_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
