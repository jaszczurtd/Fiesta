#pragma once

/**
 * @file hal_project_config.h
 * @brief JaszczurHAL project configuration for the ECU firmware.
 *
 * Declares the firmware variants. The HAL modules and settings, shared with
 * the host tests, are in ecu_hal_config.h.
 */

/* ── Variants ────────────────────────────────────────────────────────── */

/* The base image is the one for the car; BENCH adds the functional test
 * console (jh-vscode build --variant BENCH). */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(BENCH, "ECU with the functional test console",                             \
    ECU_FUNCTIONAL_TESTS_ENABLED = 1)

#include "ecu_hal_config.h"
