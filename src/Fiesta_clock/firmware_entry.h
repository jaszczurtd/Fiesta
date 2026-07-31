#ifndef FIESTA_FIRMWARE_ENTRY_H
#define FIESTA_FIRMWARE_ENTRY_H

/*
 * Entry contract consumed by the shared Fiesta application adapter.
 * Fiesta_clock is C-only and single-core, so this header exposes the C entry
 * points to the C++ adapter without enabling FIESTA_ENABLE_CORE1.
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
