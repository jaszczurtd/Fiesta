#ifndef FIESTA_FIRMWARE_ENTRY_H
#define FIESTA_FIRMWARE_ENTRY_H

/*
 * Entry contract for the CMake-generated application entry point.
 * Fiesta_clock is C-only and single-core, so this header exposes the C entry
 * points to the generated C++ .ino without enabling setup1()/loop1().
 */
#include <JaszczurHAL.h>

#ifdef __cplusplus
extern "C" {
#endif

void initialization(void);
void looper(void);

#ifdef __cplusplus
}
#endif

#endif
