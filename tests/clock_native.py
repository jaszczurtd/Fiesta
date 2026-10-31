"""Build Fiesta_clock sources natively against the module's hardware header."""

import json
import os
from pathlib import Path
import sys

from native_build import compile_sources, run_command

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "src/Fiesta_clock"
HAL = ROOT / "src/JaszczurHAL"


def build_clock_binary(build, name, sources, tree=None, defines=()):
    """Link ``sources`` into ``build / name``.

    ``tree`` replaces the module's device_tree.json, e.g. to try other pull
    modes; the module's hal_project_config.h is used as it is.
    """
    generator = [sys.executable, str(HAL / "scripts/generate_hardware_config.py")]
    output = build / "hardware"
    config = build / "config"
    config.mkdir(parents=True, exist_ok=True)
    if tree is None:
        tree = json.loads((MODULE / "device_tree.json").read_text())
    (config / "device_tree.json").write_text(json.dumps(tree))
    (config / "hal_project_config.h").write_text(
        (MODULE / "hal_project_config.h").read_text())
    common = ["--config-dir", str(config), "--output-root", str(build),
              "--output-dir", str(output)]
    run_command([*generator, "resolve", *common])
    definition_args = [arg for value in defines for arg in ("--define", value)]
    run_command([*generator, "finalize", *common, *definition_args,
                 "--hardware", str(output / "jh_hardware_resolved.json"),
                 "--hardware-header", str(output / "jh_hardware.h")])
    flags = [*("-D" + value for value in defines),
             "-Wall", "-Wextra", "-Werror", "-ffunction-sections", "-fdata-sections",
             "-DHAL_TARGET_RP2040=1",
             f'-DJH_HARDWARE_HEADER="{output / "jh_hardware.h"}"',
             "-I", str(HAL / "tests"), "-I", str(output), "-I", str(MODULE),
             "-I", str(HAL / "src"), "-I", str(HAL / "third_party/jhbase/include")]
    objects = compile_sources(sources, flags, build, run_command)
    binary = build / name
    run_command([os.environ.get("CXX", "c++"), *objects,
                 "-Wl,--gc-sections", "-o", str(binary)])
    return binary
