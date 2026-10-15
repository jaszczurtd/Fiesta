"""Exercise Fiesta_clock with the real HAL OLED renderer and a captured I2C bus."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "src/Fiesta_clock"
HAL = ROOT / "src/JaszczurHAL"


class ClockDisplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="fiesta-clock-display-test-")
        cls.addClassCleanup(cls.temp.cleanup)
        build = Path(cls.temp.name) / ".build"
        build.mkdir()
        cls.binary = build / "clock_display"
        generator = [sys.executable, str(HAL / "scripts/generate_hardware_config.py")]
        output = build / "hardware"
        common = ["--config-dir", str(MODULE), "--output-root", str(build),
                  "--output-dir", str(output)]
        cls.run_command([*generator, "resolve", *common])
        cls.run_command([*generator, "finalize", *common,
                         "--hardware", str(output / "jh_hardware_resolved.json"),
                         "--hardware-header", str(output / "jh_hardware.h")])
        flags = ["-Wall", "-Wextra", "-Werror", "-ffunction-sections", "-fdata-sections",
                 "-DHAL_TARGET_RP2040=1",
                 f'-DJH_HARDWARE_HEADER="{build / "hardware/jh_hardware.h"}"',
                 "-I", str(HAL / "tests"), "-I", str(build / "hardware"), "-I", str(MODULE), "-I", str(HAL / "src")]
        sources = [MODULE / name for name in
                   ("display.c", "clock_font.c", "main.c", "clockPart.c", "tempPart.c", "voltPart.c", "utils.c")]
        sources += [HAL / "src/hal/display" / name for name in
                    ("hal_display.cpp", "drivers/hal_display.cpp",
                     "drivers/ssd1306_driver.cpp", "drivers/jh_gfx.cpp")]
        sources.append(MODULE / "tests/test_display.cpp")
        objects = []
        for index, source in enumerate(sources):
            obj = build / f"{index}.o"
            c = source.suffix == ".c"
            cls.run_command([os.environ.get("CC" if c else "CXX", "cc" if c else "c++"),
                             "-std=gnu11" if c else "-std=c++17", *flags,
                             "-c", str(source), "-o", str(obj)])
            objects.append(str(obj))
        cls.run_command([os.environ.get("CXX", "c++"), *objects,
                         "-Wl,--gc-sections", "-o", str(cls.binary)])

    @staticmethod
    def run_command(command):
        result = subprocess.run(command, capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def test_replaces_digits_and_clips_at_panel_edge(self):
        self.run_command([str(self.binary), "text"])

    def test_init_sleep_wake_and_bus_errors(self):
        self.run_command([str(self.binary), "power"])

    def test_greeting_screens_and_idle_wakeup(self):
        self.run_command([str(self.binary), "app"])

    def test_release_prompt_reaches_panel_before_button_wait(self):
        self.run_command([str(self.binary), "prompt"])

    def test_all_original_glyphs_in_both_sizes(self):
        expected = json.loads((MODULE / "tests/glyph_sha256.json").read_text())
        result = subprocess.run([str(self.binary), "glyphs"], check=True,
                                capture_output=True, text=True, timeout=30)
        actual = [hashlib.sha256(bytes.fromhex(line)).hexdigest()
                  for line in result.stdout.splitlines()]
        self.assertEqual(actual, expected["sha256"])

    def test_local_driver_does_not_return(self):
        for name in ("lcd", "font", "twi_i2c"):
            for suffix in (".c", ".h"):
                self.assertFalse((MODULE / (name + suffix)).exists())


if __name__ == "__main__":
    unittest.main()
