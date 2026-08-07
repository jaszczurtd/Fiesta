#pragma once

/* Supplemental test-only HAL config for isolated stub consumers.
 * The main host-unit target selects tests/include first.
 */
#define HAL_ENABLE_CRYPTO /* config.cpp uses hal_base64_encode for SC_GET_META \
                           */
