"""Exercise Fiesta_clock with the real HAL OLED renderer and a captured I2C bus."""

import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from clock_native import HAL, MODULE, build_clock_binary
from native_build import run_command


class ClockDisplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="fiesta-clock-display-test-")
        cls.addClassCleanup(cls.temp.cleanup)
        build = Path(cls.temp.name) / ".build"
        build.mkdir()
        tree = json.loads((MODULE / "device_tree.json").read_text())
        # Exercise all pull modes through the production app_start().
        for node, pull in (("buttonHour", "none"), ("buttonMinute", "down"),
                           ("buttonSet", "up"), ("ignition", "down")):
            tree["nodes"][node]["properties"]["pull"] = pull
        sources = [MODULE / name for name in
                   ("display.c", "clock_font.c", "main.c", "clockPart.c", "tempPart.c", "voltPart.c", "utils.c")]
        sources += [HAL / "src/hal/display" / name for name in
                    ("hal_display.cpp", "drivers/hal_display.cpp",
                     "drivers/ssd1306_driver.cpp", "drivers/jh_gfx.cpp")]
        sources.append(MODULE / "tests/test_display.cpp")
        cls.binary = build_clock_binary(build, "clock_display", sources, tree)

    def test_replaces_digits_and_clips_at_panel_edge(self):
        run_command([str(self.binary), "text"])

    def test_init_sleep_wake_and_bus_errors(self):
        run_command([str(self.binary), "power"])

    def test_greeting_screens_and_idle_wakeup(self):
        run_command([str(self.binary), "app"])

    def test_release_prompt_reaches_panel_before_button_wait(self):
        run_command([str(self.binary), "prompt"])

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
