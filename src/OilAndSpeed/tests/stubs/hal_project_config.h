#pragma once

/* Supplemental test-only HAL config for isolated stub consumers.
 * The main host-unit target selects tests/include first.
 */
#define HAL_ENABLE_CRYPTO /* Serial session authentication and metadata. */
#define HAL_ENABLE_SERIAL_COMMANDS
#define HAL_COMMAND_ROUTER_MAX_COMMANDS 16u
