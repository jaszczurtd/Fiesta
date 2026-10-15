#include "display.h"
#include "clock_font.h"

#include <JaszczurHAL.h>

enum {
  DISPLAY_WIDTH = 128,
  DISPLAY_HEIGHT = 32,
  CELL_WIDTH = 6,
  CELL_HEIGHT = 8
};

hal_status_t clock_display_init(void) {
  hal_status_t status =
      hal_i2c_init_bus(JH_HW_BUS_I2C_INDEX, JH_HW_BUS_I2C_PIN_SDA,
                       JH_HW_BUS_I2C_PIN_SCL, JH_HW_BUS_I2C_FREQUENCY_HZ);
  if (hal_status_is_error(status)) {
    return status;
  }
  const hal_display_ssd1306_family_config_t config = {
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
  return hal_display_init_ssd1306_family_ex(&config);
}

hal_status_t clock_display_text(uint8_t column, uint8_t page, uint8_t scale,
                                const char *text) {
  const int x = column * CELL_WIDTH;
  const int y = page * CELL_HEIGHT;
  if (text == NULL || (scale != 1u && scale != 2u) || x >= DISPLAY_WIDTH ||
      y + CELL_HEIGHT * scale > DISPLAY_HEIGHT) {
    return HAL_EINVAL;
  }
  /* Bound the string so GFX cannot wrap a long field into another row. */
  char visible[DISPLAY_WIDTH / CELL_WIDTH + 1];
  const int limit = (DISPLAY_WIDTH - x) / (CELL_WIDTH * scale);
  int length = 0;
  while (length < limit && text[length] != '\0') {
    if (text[length] == '\n' || text[length] == '\r') {
      break;
    }
    visible[length] = text[length];
    ++length;
  }
  visible[length] = '\0';
  if (length == 0) {
    return HAL_OK;
  }

  return hal_display_draw_mono_text_ex(x, y, visible, &clock_font, scale,
                                       HAL_COLOR_WHITE, HAL_COLOR_BLACK);
}
