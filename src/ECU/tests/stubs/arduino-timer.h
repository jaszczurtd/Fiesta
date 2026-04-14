#pragma once
/*
 * Minimal arduino-timer.h stub for host/test builds.
 *
 * multicoreWatchdog.h includes <arduino-timer.h> in its header, but the
 * actual Timer usage lives only in multicoreWatchdog.cpp, which is excluded
 * from the test build.  This stub satisfies the header-level include so the
 * full ECU include chain compiles on the host without the Arduino SDK.
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

template<size_t max_tasks = 16, unsigned long (*time_func)() = nullptr>
struct Timer {
    void tick() {}
    template<typename H> void* in(unsigned long, H)    { return nullptr; }
    template<typename H> void* every(unsigned long, H) { return nullptr; }
    void cancel(void *) {}
};

inline Timer<> timer_create_default() { return Timer<>(); }
