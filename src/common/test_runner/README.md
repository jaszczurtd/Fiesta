# Functional-test runner

`test_runner.c` owns the registry, console command queue, sequences, atomic
parameters, deferred one-shot fixtures and SerialConfigurator supervision.
It has no ECU or VP37 dependency. One runner is initialized per firmware with
a `fiesta_test_config_t` that remains alive for the firmware lifetime.

The controller role calls `fiesta_test_tick()` and the direct start/skip/stop
functions. It owns test execution and module output callbacks. The session
role calls the console input, event drain and SC operations. Either physical
core can serve either role. Initialize before starting them; each role has
one caller. Read and write runtime parameters from either role.

Tests with a `step` callback start and run on the controller role. A test with
only `start` is a one-shot fixture: the session role first prints its start
event, then runs it and publishes completion. Stopping it cancels an unclaimed
fixture; it cannot interrupt a fixture already executing. Such a fixture must
not modify controller-owned outputs. Lifecycle messages use a bounded queue,
so a stalled console cannot block control or stopping.

The module provides its suite, parameter metadata/storage and optional policy
callbacks. `cleanup(entry, result, restart)` runs on every active-test exit,
including completion, stop, skip, replacement, loss of SC supervision and
same-test restart. It must release module outputs. A module that requires
continuous output on same-test restart can retain it when `restart` is true.
The runner does not write hardware itself. Callback strings and progress field
strings must remain valid; progress must respect its buffer capacity.

ECU keeps demand application, the 50 ms announcement wait, RPM policy and
VP37 commands in `src/ECU/tests_helpers.c`. Its existing `tests.h` API and
disabled-test stubs remain available. Console tests keep their existing
operator supervision; SC keepalive and session end apply only to SC tests.

`test_runner_sources.txt` is read by the shared CMake source loader. Firmware
manifests and host builds consume that same list. The ECU host suite includes
the runner's own module fixture and also compiles it without ECU headers or
configuration.
