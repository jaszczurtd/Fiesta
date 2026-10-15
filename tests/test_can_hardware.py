"""Pass altered assembly CAN settings through each production initializer."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from native_build import compile_sources

ROOT = Path(__file__).resolve().parents[1]
HAL = ROOT / "src/JaszczurHAL"


class CanHardwareTests(unittest.TestCase):
    def run_command(self, command):
        result = subprocess.run(command, capture_output=True, text=True, timeout=120)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)

    def test_can_initializers_use_node_properties(self):
        cases = (("ECU", "can.c", "canInit(1)", "canMain"),
                 ("ECU", "obd-2.c", "obdInit(1)", "canObd"),
                 ("Clocks", "can.cpp", "canInit()", "can"),
                 ("OilAndSpeed", "can.cpp", "canInit()", "can"),
                 ("Fiesta_clock", "can.c", "clockCanInit()", "can"))
        for module, source_name, call, node in cases:
            with self.subTest(module=module, source=source_name), tempfile.TemporaryDirectory() as temp:
                build = Path(temp) / ".build"
                build.mkdir()
                source_dir = ROOT / "src" / module
                tree = json.loads((source_dir / "device_tree.json").read_text())
                bindings = []
                for index, path in enumerate(tree.get("bindings", [])):
                    name = f"binding-{index}.json"
                    (build / name).write_text((source_dir / path).read_text())
                    bindings.append(name)
                tree["bindings"] = bindings
                tree["nodes"][node]["properties"] = {"bitrateHz": 250000, "oscillatorHz": 16000000}
                (build / "device_tree.json").write_text(json.dumps(tree))
                self.run_command([sys.executable, str(HAL / "scripts/generate_hardware_config.py"),
                                  "resolve", "--config-dir", str(build), "--output-dir", str(build),
                                  "--output-root", str(build)])
                assembly = next(iter(tree["assemblies"].values()))
                cs = assembly["nodes"][node]["pins"]["cs"]
                bus = assembly["buses"][assembly["nodes"][node]["bus"]]["index"]
                harness = build / "probe.cpp"
                declaration = ("extern \"C\" " if source_name.endswith(".c") else "")
                function = call.split("(")[0]
                argument = "int" if module == "ECU" else "void"
                harness.write_text(f'''
#include <hal/can/hal_can.h>
#include <hal/spi/hal_spi.h>
#include <hal/gpio/hal_gpio.h>
#include <hal/system/hal_system.h>
#include <cassert>
#include <cstdlib>
{declaration}hal_status_t {function}({argument});
extern "C" {{
void watchdog_feed(void) {{}}
void hal_watchdog_feed(void) {{}}
uint32_t hal_millis(void) {{ return 0; }}
void hal_gpio_set_mode(uint8_t, hal_gpio_mode_t) {{}}
void hal_gpio_write(uint8_t, bool) {{}}
hal_status_t hal_spi_init(uint8_t, uint8_t, uint8_t, uint8_t) {{ return HAL_OK; }}
hal_status_t hal_can_create_with_retry(const hal_can_config_t *cfg, uint8_t,
                                      void (*)(void), int, void (*)(void), hal_can_t *) {{
  assert(cfg->backend == HAL_CAN_BACKEND_MCP2515);
  assert(cfg->mcp2515.bitrate_hz == 250000);
  assert(cfg->mcp2515.oscillator_hz == 16000000);
  assert(cfg->mcp2515.spi_bus == {bus});
  assert(cfg->mcp2515.cs_pin == {cs});
  assert(cfg->mcp2515.one_shot_tx && cfg->mcp2515.sleep_wakeup);
  std::exit(0); // The test ends at the HAL device creation boundary.
}}
}}
int main() {{ (void){call}; return 1; }}
''')
                flags = ["-Wall", "-Wextra", "-Werror", "-O2", "-flto", "-ffunction-sections", "-fdata-sections",
                         "-DHAL_TARGET_MOCK=1", "-I", str(HAL / "src"), "-I", str(source_dir),
                         "-I", str(HAL / "third_party/cJSON"),
                         "-include", str(source_dir / "hal_project_config.h"),
                         "-include", str(build / "jh_hardware.h")]
                # Keep the real default implementation linked: reverting the fix must fail at runtime.
                objects = compile_sources(
                    (source_dir / source_name, harness,
                     HAL / "src/hal/can/mcp2515/hal_can_mcp2515_config.cpp"),
                    flags, build, self.run_command)
                binary = build / "probe"
                self.run_command([os.environ.get("CXX", "c++"), "-flto", *objects,
                                  "-Wl,--gc-sections", "-o", str(binary)])
                self.run_command([str(binary)])


if __name__ == "__main__":
    unittest.main()
