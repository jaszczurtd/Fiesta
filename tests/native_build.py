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
