"""Compile native fixtures with the compiler selected for each source language."""

import os


def compile_sources(sources, flags, build, run_command):
    objects = []
    for index, source in enumerate(sources):
        is_c = source.suffix == ".c"
        obj = build / f"{index}.o"
        run_command([os.environ.get("CC" if is_c else "CXX", "cc" if is_c else "c++"),
                     "-std=gnu11" if is_c else "-std=c++17", *flags,
                     "-c", str(source), "-o", str(obj)])
        objects.append(str(obj))
    return objects
