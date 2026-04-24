#!/usr/bin/env python3
import os
import runpy
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
COMMON_SCRIPT = os.path.join(os.path.dirname(PROJECT_DIR), "common", "scripts", "fiesta-serial-persistent.py")

sys.argv = [sys.argv[0], "--project-dir", PROJECT_DIR, *sys.argv[1:]]
runpy.run_path(COMMON_SCRIPT, run_name="__main__")
