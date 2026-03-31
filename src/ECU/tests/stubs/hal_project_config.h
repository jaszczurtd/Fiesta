#pragma once

/*
 * Test-only HAL config used by host-unit builds.
 *
 * This file shadows the project-level hal_project_config.h because tests/stubs
 * is added first on include path in CMake.
 *
 * Keep modules enabled so mock headers/types remain available.
 * Disable only what is explicitly needed for host test environment.
 */

#define HAL_DISABLE_ASSERTS
#define HAL_DISABLE_TFT
