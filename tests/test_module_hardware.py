#!/usr/bin/env python3
"""Each firmware module describes its hardware in its own device_tree.json."""

from __future__ import annotations

import json
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
HAL_ROOT = REPO_ROOT / "src" / "JaszczurHAL"
sys.path.insert(0, str(REPO_ROOT / "scripts"))

import fiesta_modules  # noqa: E402

MODULES = tuple(module.name for module in fiesta_modules.load().firmware())
POWER_STAGE = "fiesta,vp37-power-stage"
POWER_STAGE_BINDING = "../common/vp37/fiesta-vp37-power-stage.json"
# A pin, bus or address name defined straight as a number.
LITERAL_WIRING = re.compile(
    r"^\s*#\s*define\s+(\w*(?:PIN|GPIO|_CS|_INT|ADDR|SDA|SCL|MISO|MOSI|SCK|"
    r"BUS_INDEX|_4051)\w*)\s+\(?(?:0x[0-9A-Fa-f]+|\d+)[uUlL]*\)?\s*(?:/[/*].*)?$",
    re.MULTILINE,
)


# A macro that only renames a generated one; the code uses JH_HW_* directly.
JH_HW_ALIAS = re.compile(
    r"^\s*#\s*define\s+(\w+)\s+\(*(?:\(\w+\))?\s*JH_HW_\w+\)*\s*(?:/[/*].*)?$",
    re.MULTILINE,
)


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


class ModuleHardwareTests(unittest.TestCase):
    def test_manifests_select_an_assembly_of_the_device_tree(self) -> None:
        for module in MODULES:
            with self.subTest(module=module):
                tree = load_json(REPO_ROOT / "src" / module / "device_tree.json")
                manifest = load_json(
                    REPO_ROOT / "src" / module / ".vscode" / "jaszczurhal.project.json")
                self.assertNotIn("target", manifest)
                self.assertNotIn("board", manifest)
                self.assertIn(manifest.get("assembly"), tree["assemblies"])

    def test_every_assembly_resolves(self) -> None:
        # The firmware workflow builds only the CI modules; this covers all.
        with tempfile.TemporaryDirectory(prefix="fiesta-hardware-") as temp:
            root = Path(temp) / ".build"
            for module in MODULES:
                tree = load_json(REPO_ROOT / "src" / module / "device_tree.json")
                for assembly in tree["assemblies"]:
                    with self.subTest(module=module, assembly=assembly):
                        result = subprocess.run(
                            [sys.executable,
                             str(HAL_ROOT / "scripts" / "generate_hardware_config.py"),
                             "resolve", "--config-dir", str(REPO_ROOT / "src" / module),
                             "--assembly", assembly, "--output-root", str(root),
                             "--output-dir", str(root / module / assembly)],
                            capture_output=True, text=True, check=False)
                        self.assertEqual(0, result.returncode, result.stderr)

    def test_ecu_assemblies_share_the_pcb_wiring(self) -> None:
        # One ECU PCB carries either module; only the MCU target and board differ.
        assemblies = load_json(REPO_ROOT / "src" / "ECU" / "device_tree.json")["assemblies"]
        self.assertEqual({"rp2040-plus", "rp2350-plus"}, set(assemblies))

        def wiring(assembly: dict) -> dict:
            return {key: value for key, value in assembly.items()
                    if key not in ("target", "board")}

        self.assertEqual(wiring(assemblies["rp2040-plus"]), wiring(assemblies["rp2350-plus"]))

    def test_code_takes_wiring_from_the_device_tree(self) -> None:
        headers = [REPO_ROOT / "src" / module / "hardwareConfig.h" for module in MODULES]
        headers.append(REPO_ROOT / "src" / "common" / "vp37" / "vp37_power_stage.h")
        for header in headers:
            with self.subTest(header=str(header.relative_to(REPO_ROOT))):
                found = LITERAL_WIRING.findall(header.read_text(encoding="utf-8"))
                self.assertEqual([], found)

    def test_code_uses_the_generated_macros_directly(self) -> None:
        sources = [path for module in MODULES
                   for path in (REPO_ROOT / "src" / module).glob("*.[ch]*")]
        sources += list((REPO_ROOT / "src" / "common").rglob("*.[ch]"))
        for source in sources:
            with self.subTest(source=str(source.relative_to(REPO_ROOT))):
                self.assertEqual([], JH_HW_ALIAS.findall(source.read_text(encoding="utf-8")))

    def test_gps_uses_one_shared_software_serial_binding(self) -> None:
        shared = "../common/bindings/fiesta-soft-serial-gps.json"
        for module in ("ECU", "Clocks"):
            tree = load_json(REPO_ROOT / "src" / module / "device_tree.json")
            self.assertIn(shared, tree["bindings"])
            self.assertEqual("fiesta,soft-serial-gps", tree["nodes"]["gps"]["compatible"])
            self.assertTrue((REPO_ROOT / "src" / module / shared).is_file())
        clocks = load_json(REPO_ROOT / "src" / "Clocks" / "device_tree.json")
        self.assertEqual({"rx": 27, "tx": 28},
                         clocks["assemblies"]["rp2040-zero"]["nodes"]["gps"]["pins"])

    def test_vp37_power_stages_share_the_type_values(self) -> None:
        # ECU and bench wire their own pins; shunt and divider come from the
        # shared type and are never overridden per firmware.
        for module in ("ECU", "VP37TestBench"):
            with self.subTest(module=module):
                tree = load_json(REPO_ROOT / "src" / module / "device_tree.json")
                self.assertIn(POWER_STAGE_BINDING, tree["bindings"])
                self.assertEqual(POWER_STAGE, tree["nodes"]["vp37"]["compatible"])
                self.assertNotIn("properties", tree["nodes"]["vp37"])
                for assembly in tree["assemblies"].values():
                    self.assertNotIn("properties", assembly["nodes"]["vp37"])
        binding = load_json(REPO_ROOT / "src" / "common" / "vp37" / "fiesta-vp37-power-stage.json")
        defaults = {name: prop["default"] for name, prop in binding["properties"].items()}
        self.assertEqual({"shuntOhms": 0.22, "supplyDividerHighOhms": 47000,
                          "supplyDividerLowOhms": 10000}, defaults)


if __name__ == "__main__":
    unittest.main()
