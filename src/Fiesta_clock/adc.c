#include "adc.h"
#include "config.h"
#include "hardwareConfig.h"

static bool s_adc_configured = false;

static uint8_t adc_channel_to_pin(unsigned char channel) {
    /* Legacy firmware used ADC channel 6 for voltmeter input. */
    if (channel == 6u) {
        return PIN_VOLT_ADC;
    }
    return (uint8_t)channel;
}

int getADCValue(unsigned char ADCchannel) {
    if (!s_adc_configured) {
        hal_adc_set_resolution((uint8_t)CFG_ADC_RESOLUTION_BITS);
        s_adc_configured = true;
    }

    return hal_adc_read(adc_channel_to_pin(ADCchannel));
}


