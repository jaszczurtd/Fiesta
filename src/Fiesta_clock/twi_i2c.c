#include "twi_i2c.h"
#include "hardwareConfig.h"

static bool s_tx_active = false;

void TWI_Init(void) {
  hal_i2c_init_bus(JH_HW_BUS_I2C_INDEX, JH_HW_BUS_I2C_PIN_SDA,
                   JH_HW_BUS_I2C_PIN_SCL, JH_HW_BUS_I2C_FREQUENCY_HZ);
}

void TWI_Start(void) { s_tx_active = false; }

void TWI_Stop(void) {
  if (s_tx_active) {
    (void)hal_i2c_end_transmission_bus(JH_HW_BUS_I2C_INDEX);
  }
  s_tx_active = false;
}

unsigned char TWI_Read(unsigned char ack) {
  (void)ack;
  const int raw = hal_i2c_read_bus(JH_HW_BUS_I2C_INDEX);
  if (raw < 0) {
    return 0u;
  }
  return (unsigned char)raw;
}

void TWI_Write(unsigned char byte) {
  if (!s_tx_active) {
    return;
  }
  (void)hal_i2c_write_bus(JH_HW_BUS_I2C_INDEX, byte);
}

unsigned char TWI_address(unsigned char address, bool masterMode) {
  if (masterMode != MASTER_TRANSMITTER) {
    s_tx_active = false;
    return TRANSMISSION_ERROR;
  }

  hal_i2c_begin_transmission_bus(JH_HW_BUS_I2C_INDEX, address);
  s_tx_active = true;
  return TRANSMISSION_SUCCESS;
}
