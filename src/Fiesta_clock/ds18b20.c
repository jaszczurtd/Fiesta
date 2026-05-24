#include "ds18b20.h"
#include "utils.h"

#ifndef HAL_DISABLE_DS18B20

typedef struct {
    bool initialized;
    uint8_t pin;
    hal_ds18b20_t handle;
    bool conversion_started;
    bool has_value;
    float latest_temp_c;
} ds18b20_slot_t;

#ifndef DS18B20_MAX_SLOTS
#define DS18B20_MAX_SLOTS 4
#endif

static ds18b20_slot_t s_slots[DS18B20_MAX_SLOTS];
static uint8_t s_selected_pin = 0;

static ds18b20_slot_t *find_slot(uint8_t pin) {
    for (uint8_t i = 0; i < DS18B20_MAX_SLOTS; ++i) {
        if (s_slots[i].initialized && s_slots[i].pin == pin) {
            return &s_slots[i];
        }
    }
    return NULL;
}

static ds18b20_slot_t *create_slot(uint8_t pin) {
    for (uint8_t i = 0; i < DS18B20_MAX_SLOTS; ++i) {
        if (!s_slots[i].initialized) {
            hal_ds18b20_config_t cfg = {0};
            cfg.data_pin = pin;
            cfg.use_rom = false;
            cfg.resolution_hint = HAL_DS18B20_RES_12_BIT;

            hal_ds18b20_t h = hal_ds18b20_init(&cfg);
            if (!h) {
                return NULL;
            }

            s_slots[i].initialized = true;
            s_slots[i].pin = pin;
            s_slots[i].handle = h;
            s_slots[i].conversion_started = hal_ds18b20_request(h);
            s_slots[i].has_value = false;
            s_slots[i].latest_temp_c = 0.0f;
            return &s_slots[i];
        }
    }
    return NULL;
}

static ds18b20_slot_t *get_or_create_slot(uint8_t pin) {
    ds18b20_slot_t *slot = find_slot(pin);
    if (slot) {
        return slot;
    }
    return create_slot(pin);
}

static void poll_slot(ds18b20_slot_t *slot) {
    if (!slot || !slot->initialized || !slot->handle) {
        return;
    }

    if (!slot->conversion_started) {
        slot->conversion_started = hal_ds18b20_request(slot->handle);
    }

    hal_ds18b20_poll(slot->handle);

    if (!hal_ds18b20_is_busy(slot->handle)) {
        float temp_c = 0.0f;
        bool fresh = false;
        if (hal_ds18b20_take_latest(slot->handle, &temp_c, &fresh)) {
            slot->latest_temp_c = temp_c;
            slot->has_value = true;
        }
        slot->conversion_started = hal_ds18b20_request(slot->handle);
    }
}

static void poll_all_slots(void) {
    for (uint8_t i = 0; i < DS18B20_MAX_SLOTS; ++i) {
        if (s_slots[i].initialized) {
            poll_slot(&s_slots[i]);
        }
    }
}

void ds18b20_setPin(unsigned char pin) {
    s_selected_pin = (uint8_t)pin;
    (void)get_or_create_slot(s_selected_pin);
}

void ds18b20_gettemp(int *a, int *b, double *native) {
    poll_all_slots();

    ds18b20_slot_t *slot = get_or_create_slot(s_selected_pin);
    if (!slot) {
        return;
    }

    poll_slot(slot);

    if (!slot->has_value) {
        return;
    }

    if (native) {
        *native = (double)slot->latest_temp_c;
    }

    doubleToDec((double)slot->latest_temp_c, a, b);
}

#else

void ds18b20_setPin(unsigned char pin) {
    (void)pin;
}

void ds18b20_gettemp(int *a, int *b, double *native) {
    (void)a;
    (void)b;
    (void)native;
}

#endif /* HAL_DISABLE_DS18B20 */
