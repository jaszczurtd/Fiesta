"""Compile native fixtures with the compiler selected for each source language."""

import os
import subprocess


def compiler_for_source(source):
    is_c = source.suffix == ".c"
    return os.environ.get("CC" if is_c else "CXX", "cc" if is_c else "c++")


def run_command(command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    return result


def compile_sources(sources, flags, build, run_command):
    objects = []
    for index, source in enumerate(sources):
        is_c = source.suffix == ".c"
        obj = build / f"{index}.o"
        run_command([compiler_for_source(source),
                     "-std=gnu11" if is_c else "-std=c++17", *flags,
                     "-c", str(source), "-o", str(obj)])
        objects.append(str(obj))
    return objects


def run_clocks_harness(root, sources, build):
    """Build a host harness against the Clocks configuration and run it.

    The harness replaces hardware only; the listed Clocks and HAL sources are
    the production files.
    """
    hal = root / "src/JaszczurHAL"
    module = root / "src/Clocks"
    flags = ["-Wall", "-Wextra", "-Werror", "-ffunction-sections", "-fdata-sections",
             "-DHAL_TARGET_MOCK=1", "-include", str(module / "hal_project_config.h"),
             "-I", str(module), "-I", str(hal / "src"),
             "-I", str(hal / "third_party/jhbase/include")]
    objects = compile_sources(sources, flags, build, run_command)
    binary = build / "harness"
    run_command([os.environ.get("CXX", "c++"), *objects,
                 "-Wl,--gc-sections", "-o", str(binary)])
    run_command([str(binary)])
