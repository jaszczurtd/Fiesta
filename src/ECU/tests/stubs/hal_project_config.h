#pragma once

/*
 * Supplemental test-only HAL config for consumers that include tests/stubs
 * directly. The main host-unit target selects tests/include first.
 */

#define HAL_DISABLE_ASSERTS
#define HAL_ENABLE_CRYPTO /* config.c uses hal_base64_encode for SC_GET_META   \
                           */
