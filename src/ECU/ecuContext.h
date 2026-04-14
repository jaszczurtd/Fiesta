#ifndef ECU_CONTEXT_H
#define ECU_CONTEXT_H

/*
 * Central ECU context — single struct owning all module instances.
 *
 * INCLUDE ONLY FROM .cpp FILES, after the module's own header has been
 * included first. Reason: module headers form a circular include chain
 * (each includes start.h which includes all others). By the time the
 * first #include in a .cpp is processed, all module typedefs are already
 * defined, so ecuContext.h can safely use them here. Including this file
 * from a .h would break that assumption and cause "type undefined" errors.
 */

#include "engineFan.h"
#include "engineHeater.h"
#include "heatedWindshield.h"
#include "glowPlugs.h"
#include "rpm.h"
#include "turbo.h"
#include "vp37.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    engineFan         fan;
    engineHeater      heater;
    heatedWindshields windows;
    glowPlugs         glowP;
    RPM               rpm;
    Turbo             turbo;
    VP37Pump          injectionPump;
} ecu_context_t;

ecu_context_t *getECUContext(void);

#ifdef __cplusplus
}
#endif

#endif
