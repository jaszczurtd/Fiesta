"""Exercise ECU I/O sampling phases after delayed core-0 polls and tick wrap."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from native_build import compile_sources, run_command

ROOT = Path(__file__).resolve().parents[1]
HAL = ROOT / "src/JaszczurHAL"


class StartCadenceTests(unittest.TestCase):
    def test_delayed_polls_keep_rates_without_catch_up_bursts(self):
        module = ROOT / "src/ECU"
        with tempfile.TemporaryDirectory(prefix="fiesta-cadence-") as temporary:
            build = Path(temporary) / ".build"
            build.mkdir()
            run_command([sys.executable, str(HAL / "scripts/generate_hardware_config.py"),
                         "resolve", "--config-dir", str(module), "--assembly", "rp2350-plus",
                         "--output-root", str(build), "--output-dir", str(build)])
            source = build / "cadence.c"
            source.write_text('''
#include <assert.h>
#include <stdint.h>
static uint32_t clock_ms;
static unsigned high_calls, status_calls;
uint32_t hal_millis(void) { return clock_ms; }
void readHighValues(void) { ++high_calls; }
void CAN_updaterecipients_01(void) { ++status_calls; }
#include ''' + json.dumps((module / "start.c").as_posix()) + '''
static void reset(uint32_t now) {
  clock_ms = now;
  high_calls = status_calls = 0;
  start_resetFastIOCadence();
}
static void poll(uint32_t now) {
  clock_ms = now;
  start_tickFastIO();
}
int main(void) {
  reset(0);
  for (uint32_t now = 0; now <= 1000; now += 10) { poll(now); }
  // 62 ms sampling: 16 calls, even when polls land on 10 ms boundaries.
  assert(high_calls == 16 && status_calls == 20);

  reset(0);
  poll(1000); // A long pause serves only the newest snapshot.
  assert(high_calls == 1 && status_calls == 1);
  poll(1001);
  assert(high_calls == 1 && status_calls == 1);
  poll(1050);
  assert(high_calls == 1 && status_calls == 2);
  poll(1054); // The 17th sampling slot, not 62 ms after the late poll.
  assert(high_calls == 2 && status_calls == 2);

  const uint32_t start = UINT32_MAX - 10u;
  reset(start);
  poll(start + 49u);
  assert(high_calls == 0 && status_calls == 0);
  poll(start + 50u);
  assert(high_calls == 0 && status_calls == 1);
  poll(start + 61u);
  assert(high_calls == 0 && status_calls == 1);
  poll(start + 62u);
  assert(high_calls == 1 && status_calls == 1);
  poll(start + 100u);
  assert(high_calls == 1 && status_calls == 2);
  return 0;
}
''')
            flags = ["-Wall", "-Wextra", "-Werror", "-O2", "-flto",
                     "-ffunction-sections", "-fdata-sections", "-DHAL_TARGET_MOCK=1",
                     "-I", str(HAL / "src"), "-I", str(module),
                     "-I", str(HAL / "third_party/cJSON"),
                     "-I", str(HAL / "third_party/jhbase/include"),
                     "-include", str(module / "hal_project_config.h"),
                     "-include", str(build / "jh_hardware.h")]
            for bench in (0, 1):
                with self.subTest(bench=bench):
                    mode_flags = [*flags, f"-DECU_FUNCTIONAL_TESTS_ENABLED={bench}"]
                    objects = compile_sources((source,), mode_flags, build, run_command)
                    binary = build / f"cadence-{bench}"
                    run_command([os.environ.get("CC", "cc"), "-flto", *objects,
                                 "-Wl,--gc-sections", "-o", str(binary)])
                    run_command([str(binary)])


if __name__ == "__main__":
    unittest.main()
