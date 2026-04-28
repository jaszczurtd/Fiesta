#pragma once
/* Test-only override: enable all HAL modules so that mock types compile.
 * The real hal_project_config.h disables modules not used on the RP2040
 * target, but hal_mock.h needs all type definitions available. */

/* Display: keep types but skip TFT driver selection.
 * Do NOT also define HAL_DISABLE_SSD1306 - hal_config.h would propagate
 * both into HAL_DISABLE_DISPLAY, hiding hal_font_id_t. */
#define HAL_DISABLE_TFT
