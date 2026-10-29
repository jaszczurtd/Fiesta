"""Exercise the Fiesta_clock time code over the HAL RTC stack and a PCF8563 model."""

from pathlib import Path
import tempfile
import unittest

from clock_native import HAL, MODULE, build_clock_binary
from native_build import run_command

ROOT = MODULE.parents[1]


class ClockTimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="fiesta-clock-time-test-")
        cls.addClassCleanup(cls.temp.cleanup)
        build = Path(cls.temp.name) / ".build"
        build.mkdir()
        sources = [MODULE / name for name in
                   ("RTC.c", "clockTime.c", "rtcSync.c", "can.c", "config.c",
                    "clockPart.c", "ds18b20.c", "utils.c")]
        sources.append(ROOT / "src/common/scDefinitions/sc_param_handlers.c")
        sources += [HAL / "src/hal" / name for name in
                    ("rtc/hal_rtc.cpp", "rtc/jh_rtc_i2c_provider.cpp",
                     "impl/rp2040/jh_rp_rtc_provider.cpp",
                     "rtc/pcf8563/pcf8563.cpp", "time/hal_time.cpp",
                     "time/jh_calendar.c")]
        sources.append(MODULE / "tests/test_time.cpp")
        cls.binary = build_clock_binary(build, "clock_time", sources)

    def run_scenario(self, name):
        self.assertEqual(run_command([str(self.binary), name]).stdout, "PASS\n")

    def test_local_time_conversion_and_calendar_edges(self):
        self.run_scenario("local")

    def test_utc_marker_in_kv(self):
        self.run_scenario("marker")

    def test_change_to_utc_follows_the_flash_answer(self):
        self.run_scenario("transition")

    def test_rtc_frame_carries_trusted_utc_only(self):
        self.run_scenario("rtc_frame")

    def test_first_gps_time_sets_the_rtc_which_then_runs_alone(self):
        self.run_scenario("sync_first")

    def test_legacy_local_time_is_replaced(self):
        self.run_scenario("sync_legacy")

    def test_correction_tolerance_and_interval(self):
        self.run_scenario("sync_correction")

    def test_rtc_and_kv_errors_wait_for_the_retry(self):
        self.run_scenario("sync_errors")

    def test_stale_invalid_and_edited_frames_are_not_used(self):
        self.run_scenario("sync_rejects")

    def test_configurator_uses_local_time(self):
        self.run_scenario("sc")

    def test_buttons_use_local_time(self):
        self.run_scenario("buttons")

    def test_air_temperatures_on_change_and_every_second(self):
        self.run_scenario("air_temp")

    def test_air_temperature_of_a_missing_sensor(self):
        self.run_scenario("air_temp_missing")


if __name__ == "__main__":
    unittest.main()
