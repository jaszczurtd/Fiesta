
#include "Cluster.h"

cluster_s Cluster::speedometer;
cluster_s Cluster::tachometer;

static constexpr float SPEED_COEFFICIENT = 1.2884f;
static constexpr float SPEED_OFFSET = 1.86f;

static constexpr float RPM_COEFFICIENT = 0.0332143f;
static constexpr float RPM_OFFSET = 2.14286f;

static int64_t toggle_callback(alarm_id_t id, void *user_data) {
  cluster_s *data = static_cast<cluster_s*>(user_data);
  if(data != nullptr) {
    gpio_put(data->pin, data->state);
    data->state = !data->state;
    add_alarm_in_us(data->half_period_us, toggle_callback, user_data, false);
  }
  return 0;
}

Cluster::Cluster() {
  init_output(SPEED_OUTPUT_PIN);
  init_output(TACHO_OUTPUT_PIN);
}

uint32_t Cluster::calculate_period(uint freq) {
  return (1000000 / freq) / 2;
}

uint Cluster::calculate_freq_from_speed(uint speed) {
  int freq = int(SPEED_COEFFICIENT * float(speed) + SPEED_OFFSET);
  return (freq < 1) ? 1 : freq;
}

uint Cluster::calculate_freq_from_rpm(uint rpm) {
  int freq = int(RPM_COEFFICIENT * float(rpm) + RPM_OFFSET);
  return (freq < 1) ? 1 : freq;
}

void Cluster::calculate_freq_half_period(uint pin, uint value) {
  switch(pin) {
    case SPEED_OUTPUT_PIN:
      speedometer.freq = calculate_freq_from_speed(value);
      speedometer.half_period_us = calculate_period(speedometer.freq);
    break;

    case TACHO_OUTPUT_PIN:
      tachometer.freq = calculate_freq_from_rpm(value);
      tachometer.half_period_us = calculate_period(tachometer.freq);
    break;
  }
}

void Cluster::init_output(uint pin) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_OUT);
  gpio_put(pin, 0);

  calculate_freq_half_period(pin, 1);
  switch(pin) {
    case SPEED_OUTPUT_PIN:
      speedometer.pin = pin;
      add_alarm_in_us(speedometer.half_period_us, toggle_callback, &speedometer, false);
    break;

    case TACHO_OUTPUT_PIN:
      tachometer.pin = pin;
      add_alarm_in_us(tachometer.half_period_us, toggle_callback, &tachometer, false);
    break;
  }
}

void Cluster::update(uint speed, uint rpm) {
  calculate_freq_half_period(SPEED_OUTPUT_PIN, speed);
  calculate_freq_half_period(TACHO_OUTPUT_PIN, rpm);
}