#pragma once

#include <hal/core/hal_status.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Configure the 128x32 OLED, including on subsequent ignition changes.
 * Reapplies panel settings as the original clock did. Called only from core 0.
 * @return The first HAL error, or HAL_OK. */
hal_status_t clock_display_init(void);

/** Replace text in the clock's 6x8 pixel grid, using the original clock font.
 * @param column Horizontal position in 6-pixel cells, independent of scale.
 * @param page Vertical position in 8-pixel rows, independent of scale.
 * @param scale Font scale: 1 or 2.
 * @param text Non-NULL, NUL-terminated single-line text. Only whole cells that
 * fit the panel are drawn; spaces erase old digits. Writes immediately, without
 * a framebuffer.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, or a HAL display error. */
hal_status_t clock_display_text(uint8_t column, uint8_t page, uint8_t scale,
                                const char *text);

#ifdef __cplusplus
}
#endif
