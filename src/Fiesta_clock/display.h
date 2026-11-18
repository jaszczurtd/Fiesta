#pragma once

#include <hal/core/hal_status.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Text cells in one row of the 128x32 panel at scale 1 (6-pixel cells). */
#define CLOCK_DISPLAY_COLUMNS 21u

/** Release a blocked I2C bus (bus clear), configure the bus and the 128x32
 * OLED, then clear it. Used at start, on ignition changes and to recover from
 * a failed panel write. Called only from core 0.
 * @return The first HAL error, or HAL_OK. */
hal_status_t clock_display_init(void);

/** Send the full panel configuration again and clear the panel, keeping the
 * bus as it is. Used wherever the whole screen changes (wake-up, screen and
 * set-mode changes), so a panel that lost its settings or its RAM, e.g. after
 * a supply dip, shows correct content again without an extra blink.
 * @return HAL_OK or the HAL display error. */
hal_status_t clock_display_restart(void);

/** Clear the panel RAM without configuring the panel again. Every clear of
 * the panel goes through this module, which remembers what the panel shows.
 * @return HAL_OK or the HAL display error. */
hal_status_t clock_display_clear(void);

/** Replace text in the clock's 6x8 pixel grid, using the original clock font.
 * The module remembers the glyph shown in every cell and sends only the span
 * of glyphs that differ, so a screen drawn again unchanged causes no I2C
 * traffic. A failed write forgets what the panel shows, so the next draw sends
 * everything again.
 * @param column Horizontal position in 6-pixel cells, independent of scale.
 * @param page Vertical position in 8-pixel rows, independent of scale.
 * @param scale Font scale: 1 or 2.
 * @param text Non-NULL, NUL-terminated single-line text. Only whole cells that
 * fit the panel are drawn; spaces erase old digits. Writes immediately, without
 * a framebuffer.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, or a HAL display error. */
hal_status_t clock_display_text(uint8_t column, uint8_t page, uint8_t scale,
                                const char *text);

/** Like clock_display_text(), but pad the text with spaces up to end_column,
 * so a shorter text also erases what a longer one left in its field.
 * @param end_column First 6-pixel column after the field, at least column and
 * at most CLOCK_DISPLAY_COLUMNS; only whole cells of the scale fill the field.
 * @return HAL_OK, HAL_EINVAL for invalid arguments, or a HAL display error. */
hal_status_t clock_display_field(uint8_t column, uint8_t page, uint8_t scale,
                                 uint8_t end_column, const char *text);

/** Recover from a failed panel write: log it and run clock_display_init(), at
 * most once a second until it succeeds. Every CFG_DISPLAY_REFRESH_MS it also
 * forgets what the panel shows, so the next pass sends the whole screen again
 * and repairs content that changed unnoticed. Call once per main loop pass,
 * before drawing; every screen draws its whole content each pass. */
void clock_display_service(void);

#ifdef __cplusplus
}
#endif
