#include "twi_i2c.h"
#include "hardwareConfig.h"

static bool s_tx_active = false;

void TWI_Init(void) {
    hal_i2c_init_bus(I2C_BUS_INDEX, PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);
}

void TWI_Start(void) {
    s_tx_active = false;
}

void TWI_Stop(void) {
    if (s_tx_active) {
        (void)hal_i2c_end_transmission_bus(I2C_BUS_INDEX);
    }
    s_tx_active = false;
}

unsigned char TWI_Read(unsigned char ack) {
    (void)ack;
    const int raw = hal_i2c_read_bus(I2C_BUS_INDEX);
    if (raw < 0) {
        return 0u;
    }
    return (unsigned char)raw;
}

void TWI_Write(unsigned char byte) {
    if (!s_tx_active) {
        return;
    }
    (void)hal_i2c_write_bus(I2C_BUS_INDEX, byte);
}

unsigned char TWI_address(unsigned char address, bool masterMode) {
    if (masterMode != MASTER_TRANSMITTER) {
        s_tx_active = false;
        return TRANSMISSION_ERROR;
    }

    hal_i2c_begin_transmission_bus(I2C_BUS_INDEX, address);
    s_tx_active = true;
    return TRANSMISSION_SUCCESS;
}

