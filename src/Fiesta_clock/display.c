#include "display.h"
#include "clock_font.h"
#include "config.h"

#include <JaszczurHAL.h>

enum {
  DISPLAY_WIDTH = 128,
  DISPLAY_HEIGHT = 32,
  CELL_WIDTH = 6,
  CELL_HEIGHT = 8,
  PAGES = DISPLAY_HEIGHT / CELL_HEIGHT,
  /* A panel that keeps failing is retried at this pace, not every pass. */
  RETRY_INTERVAL_MS = 1000
};

JB_STATIC_ASSERT(CLOCK_DISPLAY_COLUMNS == DISPLAY_WIDTH / CELL_WIDTH,
                 "CLOCK_DISPLAY_COLUMNS must match the panel width");

static const hal_display_ssd1306_family_config_t s_config = {
    .controller = HAL_DISPLAY_OLED_CONTROLLER_SSD1306,
    .bus_type = HAL_DISPLAY_OLED_BUS_I2C,
    .width = DISPLAY_WIDTH,
    .height = DISPLAY_HEIGHT,
    .bus = JH_HW_BUS_I2C_INDEX,
    .i2c_addr = JH_HW_NODE_OLED_ADDRESS,
    .rst_pin = -1,
    .switchvcc = HAL_DISPLAY_VCC_SWITCHCAP,
    .clock_hz = JH_HW_BUS_I2C_FREQUENCY_HZ,
    .orientation = HAL_DISPLAY_OLED_ORIENTATION_NATIVE,
    .buffer_mode = HAL_DISPLAY_UNBUFFERED,
    .override_panel = true,
    .panel =
        {
            .contrast = 0x3f,
            .clock_div = 0xf0,
            .precharge = 0x22,
            .vcomh = 0x20,
            .pump_voltage_command = 0,
            .deactivate_scroll = false,
        },
};

/* Set by any failed panel write; clock_display_service() then initialises
 * the bus and the panel again. */
static bool s_write_failed = false;
static uint32_t s_last_retry_ms = 0u;
static uint32_t s_last_refresh_ms = 0u;

/* What every 6x8 cell of the panel shows: unknown, blank, or one cell of a
 * glyph (character, scale and the cell's place inside a scale-2 glyph). A
 * screen that draws the same text again then sends nothing over I2C, which
 * keeps the shared bus idle almost all the time. */
enum { CELL_UNKNOWN = 0u, CELL_BLANK = 1u, CELL_GLYPH = 0x8000u };
static uint16_t s_cells[PAGES][CLOCK_DISPLAY_COLUMNS];

static void cells_fill(uint16_t value) {
  for (unsigned page = 0u; page < (unsigned)PAGES; ++page) {
    for (unsigned column = 0u; column < CLOCK_DISPLAY_COLUMNS; ++column) {
      s_cells[page][column] = value;
    }
  }
}

static uint16_t glyph_cell(char ch, unsigned scale, unsigned dx, unsigned dy) {
  return (uint16_t)(CELL_GLYPH | (unsigned)(uint8_t)ch | (scale << 8) |
                    (dx << 10) | (dy << 11));
}

/* Whether the glyph at cell (column, page) is on the panel already. The
 * caller keeps the glyph's cells inside the panel. */
static bool glyph_shown(unsigned column, unsigned page, unsigned scale,
                        char ch) {
  for (unsigned dy = 0u; dy < scale; ++dy) {
    for (unsigned dx = 0u; dx < scale; ++dx) {
      if (s_cells[page + dy][column + dx] != glyph_cell(ch, scale, dx, dy)) {
        return false;
      }
    }
  }
  return true;
}

static void glyph_store(unsigned column, unsigned page, unsigned scale,
                        char ch) {
  for (unsigned dy = 0u; dy < scale; ++dy) {
    for (unsigned dx = 0u; dx < scale; ++dx) {
      s_cells[page + dy][column + dx] = glyph_cell(ch, scale, dx, dy);
    }
  }
}

/* Every HAL error of a panel write leaves the panel in an unknown state;
 * invalid arguments return before the HAL is called. */
static hal_status_t track(hal_status_t status) {
  if (hal_status_is_error(status)) {
    s_write_failed = true;
    cells_fill(CELL_UNKNOWN);
  }
  return status;
}

/* The HAL panel init and a fill both leave the panel RAM blank. */
static hal_status_t cleared(hal_status_t status) {
  if (!hal_status_is_error(status)) {
    cells_fill(CELL_BLANK);
  }
  return status;
}

hal_status_t clock_display_init(void) {
  /* A disturbed transfer can leave the RTC or the panel holding SDA low; up to
   * nine SCL pulses and a STOP release it before the controller starts again.
   * Re-initialising the controller alone does not. */
  hal_status_t status = hal_i2c_bus_clear_bus(
      JH_HW_BUS_I2C_INDEX, JH_HW_BUS_I2C_PIN_SDA, JH_HW_BUS_I2C_PIN_SCL);
  if (!hal_status_is_error(status)) {
    status =
        hal_i2c_init_bus(JH_HW_BUS_I2C_INDEX, JH_HW_BUS_I2C_PIN_SDA,
                         JH_HW_BUS_I2C_PIN_SCL, JH_HW_BUS_I2C_FREQUENCY_HZ);
  }
  if (!hal_status_is_error(status)) {
    status = hal_display_init_ssd1306_family_ex(&s_config);
  }
  s_write_failed = hal_status_is_error(status);
  cells_fill(s_write_failed ? CELL_UNKNOWN : CELL_BLANK);
  return status;
}

hal_status_t clock_display_restart(void) {
  /* The HAL init sends every panel setting and clears the RAM. */
  return cleared(track(hal_display_init_ssd1306_family_ex(&s_config)));
}

hal_status_t clock_display_clear(void) {
  return cleared(track(hal_display_fill_screen_ex(HAL_COLOR_BLACK)));
}

static hal_status_t draw_cells(uint8_t column, uint8_t page, uint8_t scale,
                               uint8_t end_column, bool pad, const char *text) {
  const int x = column * CELL_WIDTH;
  const int y = page * CELL_HEIGHT;
  if (text == NULL || (scale != 1u && scale != 2u) || x >= DISPLAY_WIDTH ||
      y + CELL_HEIGHT * scale > DISPLAY_HEIGHT || end_column < column ||
      end_column > CLOCK_DISPLAY_COLUMNS) {
    return HAL_EINVAL;
  }
  /* Bound the string so GFX cannot wrap a long field into another row. */
  char visible[CLOCK_DISPLAY_COLUMNS + 1u];
  int limit = (DISPLAY_WIDTH - x) / (CELL_WIDTH * scale);
  if (pad && (end_column - column) / scale < limit) {
    limit = (end_column - column) / scale;
  }
  int length = 0;
  while (length < limit && text[length] != '\0' && text[length] != '\n' &&
         text[length] != '\r') {
    visible[length] = text[length];
    ++length;
  }
  while (pad && length < limit) {
    visible[length++] = ' ';
  }
  visible[length] = '\0';
  /* Send only the span from the first to the last glyph that differs. */
  int first = -1;
  int last = -1;
  for (int i = 0; i < length; ++i) {
    if (!glyph_shown(column + (unsigned)i * scale, page, scale, visible[i])) {
      if (first < 0) {
        first = i;
      }
      last = i;
    }
  }
  if (first < 0) {
    return HAL_OK;
  }
  visible[last + 1] = '\0';
  const int span_x = x + first * CELL_WIDTH * scale;
  const hal_status_t status = track(
      hal_display_draw_mono_text_ex(span_x, y, &visible[first], &clock_font,
                                    scale, HAL_COLOR_WHITE, HAL_COLOR_BLACK));
  if (!hal_status_is_error(status)) {
    for (int i = first; i <= last; ++i) {
      glyph_store(column + (unsigned)i * scale, page, scale, visible[i]);
    }
  }
  return status;
}

hal_status_t clock_display_text(uint8_t column, uint8_t page, uint8_t scale,
                                const char *text) {
  return draw_cells(column, page, scale, CLOCK_DISPLAY_COLUMNS, false, text);
}

hal_status_t clock_display_field(uint8_t column, uint8_t page, uint8_t scale,
                                 uint8_t end_column, const char *text) {
  return draw_cells(column, page, scale, end_column, true, text);
}

void clock_display_service(void) {
  const uint32_t now_ms = hal_millis();
  if (hal_millis_interval_elapsed(now_ms, &s_last_refresh_ms,
                                  CFG_DISPLAY_REFRESH_MS)) {
    cells_fill(CELL_UNKNOWN);
  }
  if (!s_write_failed || !hal_millis_interval_elapsed(now_ms, &s_last_retry_ms,
                                                      RETRY_INTERVAL_MS)) {
    return;
  }
  derr_limited("oled", "OLED write failed; configuring the panel again");
  (void)clock_display_init();
}
