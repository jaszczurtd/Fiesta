
//Fiesta MK5 clusters (RPM/Speed)

#include "Cluster.h"

static int64_t toggle_callback(alarm_id_t id, void *user_data) {
  cluster_s *data = (cluster_s*)user_data;
  if(data != NULL) {
    gpio_put(data->pin, data->state);
    data->state = !data->state;
    add_alarm_in_us(data->half_period_us, toggle_callback, user_data, false);
  }
  return 0;
}

Cluster::Cluster() {
  init_output(SPEED_OUTPUT_PIN, 10);
  init_output(TACHO_OUTPUT_PIN, 100);
}

uint32_t Cluster::calculate_period(uint freq) {
  return (1000000 / freq) / 2;
}

uint Cluster::calculate_freq_from_speed(float speed) {
  int freq = int(1.3513f * speed - 2.50f);
  if(freq < 1) {
    freq = 1;
  }
  return freq;
}

uint Cluster::calculate_freq_from_rpm(float rpm) {
  int freq = int(0.0332143f * rpm + 2.14286f);
  if(freq < 1) {
    freq = 1;
  }
  return freq;
}

cluster_s Cluster::speedometer = {0, false, SPEED_OUTPUT_PIN, 1};  
cluster_s Cluster::tachometer = {0, false, TACHO_OUTPUT_PIN, 1};  

void Cluster::init_output(uint pin, int value) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_OUT);
  gpio_put(pin, 0);

  switch(pin) {
    case SPEED_OUTPUT_PIN:
      speedometer.pin = pin;
      speedometer.freq = calculate_freq_from_speed(value);
      speedometer.half_period_us = calculate_period(speedometer.freq);
      add_alarm_in_us(speedometer.half_period_us, toggle_callback, &speedometer, false);
    break;

    case TACHO_OUTPUT_PIN:
      tachometer.pin = pin;
      tachometer.freq = calculate_freq_from_rpm(value);
      tachometer.half_period_us = calculate_period(tachometer.freq);
      add_alarm_in_us(tachometer.half_period_us, toggle_callback, &tachometer, false);
    break;
  }
}

void Cluster::update(int speed, int rpm) {
  speedometer.freq = calculate_freq_from_speed(speed);
  speedometer.half_period_us = calculate_period(speedometer.freq);

  tachometer.freq = calculate_freq_from_rpm(rpm);
  tachometer.half_period_us = calculate_period(tachometer.freq);
}