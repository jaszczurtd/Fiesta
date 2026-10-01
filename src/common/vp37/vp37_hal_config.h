#pragma once

/**
 * @file vp37_hal_config.h
 * @brief HAL settings every firmware driving the VP37 module needs.
 *
 * Included from the board's hal_project_config.h, before hal_config.h applies
 * its defaults. vp37_adjustometer.c stops the build when the bound is missing.
 */

/* Bound failed feedback transfers; the 30-byte frame normally needs < 1 ms. */
#define HAL_RP_I2C_TIMEOUT_US 2000U
