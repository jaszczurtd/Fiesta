#!/usr/bin/env python3
import os
import importlib.util
import json
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
COMMON_SCRIPT = os.path.join(os.path.dirname(PROJECT_DIR), "common", "scripts", "fiesta-serial-persistent.py")

spec = importlib.util.spec_from_file_location("fiesta_serial_persistent", COMMON_SCRIPT)
module = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(module)


def find_settings():
    settings_path = os.path.join(PROJECT_DIR, ".vscode", "settings.json")

    if os.path.isfile(settings_path):
        try:
            with open(settings_path, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            return {}

    return {}


module.find_settings = find_settings
module.signal.signal(
    module.signal.SIGINT,
    lambda s, f: (print(f"\n{module.CYAN}Done.{module.NC}"), module.sys.exit(0)),
)
module.main()
