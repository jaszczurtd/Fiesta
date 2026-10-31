# Host tests and CI

[Polski](TESTING.pl.md) · [Setup](BUILDING.md) · [MISRA](../MISRA.md)

The module registry in [`modules.json`](../modules.json) determines local
firmware and host-test lists. Firmware modules with `hostTests` use the HAL
mock and C++ test targets; SerialConfigurator has its own C test project.
Fiesta_clock has firmware builds and a display test launched by the root
Python test suite. It compiles the production UI, HAL renderer and SSD1306
driver against simulated board I/O. A controller RAM model interprets I²C
addressing and data writes; glyph images in both sizes are compared against
recordings from the original driver. Tests also cover sleep/wake commands.
A second test compiles the clock's time and CAN code with the HAL RTC driver
against a PCF8563 register model, a DS18B20 model and simulated CAN, flash and
configurator calls. It checks the UTC marker, CET/CEST conversion, setting the
RTC from GPS UTC, the `0x130` and `0x132` frames, and time edits from the
buttons and the configurator.
They need a host C/C++ compiler and no connected board. They do not establish
physical panel compatibility or measure hardware timing.
VP37TestBench participates in local builds and tests, but its `ci: false`
excludes it from the firmware CI matrix.

## Run local checks

After [preparing the tools](BUILDING.md#prepare-linux-or-wsl), run from the root:

```bash
./runalltests.sh -j8
```

The runner checks tool availability, builds host projects, runs runtime CTest
cases with `-LE static-analysis`, then runs cppcheck for modules selecting it,
the MISRA gate, PMD CPD, Valgrind and clang-tidy. Static analysis runs separately
so its duration does not look like a stalled runtime test. Logs are written
under `/tmp/fiesta_*`.

Every host-tested module builds and runs its tests twice: in Debug, and in
Release as CI does, under `<build dir>/release` (for example
`src/SerialConfigurator/build/release`). Release also checks optimized builds
and the compiler's enabled libc hardening. The Valgrind and clang-tidy stages
check both builds of every module.

GUI tests run with the current display or Xvfb through
`scripts/sc-with-display.sh`. A missing display/Xvfb is an error. The live
Clocks view runs in the current locale and explicitly in `C` and every
locale listed in `scripts/test-locales.txt`; the setup script generates those
and checks that they work. On Debian and Raspberry Pi OS `locale-gen` builds
only the entries enabled in `/etc/locale.gen`, so the script enables them
there first.
The same runner wraps the SC Valgrind target, so Valgrind checks the test
binary itself. Suppressions cover the documented system GTK/fontconfig
stacks and the glibc loader during GTK start-up; application errors remain
failures. Started directly without a display, the progress-bar and Tests-tab
tests report a skip in CTest.

`--skip-cppcheck`, `--skip-valgrind` and `--skip-clang-tidy` omit individual
checks for development. Such a run does not establish that the complete gate
passes. `-j<N>` controls build parallelism.

The MISRA gate rejects active findings in the shared VP37 and Adjustometer
scope after documented suppressions; the rest of ECU remains a screening
report. See the [scope and deviation policy](../MISRA.md).

PMD CPD uses the version selected by HAL and thresholds of 100 tokens for
C/C++ and 50 for Python. Production code is compared within each module;
host tests and Python are compared across the repository. Any duplicate group
fails the gate. Shared test helpers belong in [`src/common/tests/`](../src/common/tests/).

## Test one module

Example for ECU, from the repository root:

```bash
cmake -S src/ECU -B src/ECU/build_test -DCMAKE_BUILD_TYPE=Release
cmake --build src/ECU/build_test --parallel
ctest --test-dir src/ECU/build_test --output-on-failure -LE static-analysis
```

Use the corresponding module directory for Clocks, Adjustometer
or VP37TestBench. For the desktop application follow the
[SerialConfigurator guide](../src/SerialConfigurator/README.md#tests).
List registered tests with `ctest --test-dir src/ECU/build_test -N`.
Listing tests does not execute them.

Run the Fiesta_clock display and time tests separately with:

```bash
python3 -m unittest discover -s tests -p 'test_clock_*.py' -v
```

## Validate a HAL update

Follow [DEPENDENCIES.md](../DEPENDENCIES.md). In addition to the host gate,
build release and debug firmware for **every firmware entry** in `modules.json`,
including VP37TestBench, and check declared variants such as ECU BENCH.
The [build guide](BUILDING.md#maintain-generated-configuration) gives the
registry and VS Code generation checks. CI's selection is narrower than this
local firmware validation.

## CI scope

| Workflow | Checks |
|---|---|
| [ecu-tests.yml](../.github/workflows/ecu-tests.yml) | ECU build, tests, cppcheck, Valgrind and clang-tidy; the root Python tests, also after changes in the modules they read. |
| [clocks-tests.yml](../.github/workflows/clocks-tests.yml), [adjustometer-tests.yml](../.github/workflows/adjustometer-tests.yml) | Module build, tests, Valgrind and clang-tidy. |
| [serial-configurator-tests.yml](../.github/workflows/serial-configurator-tests.yml) | GUI/CLI build, tests, Valgrind and clang-tidy. |
| [firmware-build-scripts.yml](../.github/workflows/firmware-build-scripts.yml) | Release/debug builds for firmware selected for CI. |
| [windows-firmware.yml](../.github/workflows/windows-firmware.yml) | Generation checks, firmware builds and compile databases on Windows for the same CI selection. |
| [ecu-cppcheck.yml](../.github/workflows/ecu-cppcheck.yml) | Manual cppcheck comparison with the ECU baseline. |
| [ecu-misra.yml](../.github/workflows/ecu-misra.yml) | Manual MISRA screening artifacts. |

Path filters in the workflows select runs after module, shared-code, tooling
or HAL-pointer changes. [`tests/test_module_registry.py`](../tests/test_module_registry.py)
checks the module lists against the registry. Physical upload remains a
manual check; a successful firmware build does not demonstrate device behaviour.

The [daily systemd runner](../src/ECU/scripts/systemd/README.md) runs setup,
tests and builds on a Linux machine and emails the result. It uses a checkout
that it resets to the selected remote branch; do not use a working copy with
local development changes for that service.
