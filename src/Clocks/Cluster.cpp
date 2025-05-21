
#include "Cluster.h"

cluster_s Cluster::speedometer;
cluster_s Cluster::tachometer;

static constexpr float SPEED_OFFSET = 1.86f;
static constexpr float RPM_COEFFICIENT = 0.0332143f;
static constexpr float RPM_OFFSET = 2.14286f;

struct CalibrationPoint {
    uint16_t speed;
    float frequency;
};

constexpr std::array<CalibrationPoint, 13> SPEED_CALIBRATION = {{
  {0,   2.0f}, 
  {20,  23.5f},
  {30,  36.0f},   
  {50,  66.9f},   
  {60,  80.0f},   
  {80,  106.6f},  
  {120, 158.1f},  
  {150, 198.2f},
  {180, 236.9f},
  {190, 248.6f},
  {200, 262.9f}, 
  {210, 275.8f},
  {220, 290.5f}
}};

static int64_t toggle_callback(alarm_id_t id, void *user_data) {
  cluster_s *data = static_cast<cluster_s*>(user_data);
  if(data != nullptr) {
    gpio_put(data->pin, data->state);
    data->state = !data->state;
    data->alarm = add_alarm_in_us(data->half_period_us, toggle_callback, user_data, false);
  }
  return 0;
}

float calculate_required_coefficient(uint16_t speed, float target_freq) {
  return (target_freq - SPEED_OFFSET) / speed;
}

float calculate_dynamic_coefficient(uint16_t speed) {
  for(size_t i = 0; i < SPEED_CALIBRATION.size() - 1; ++i) {
    if(speed >= SPEED_CALIBRATION[i].speed && speed <= SPEED_CALIBRATION[i+1].speed) {
      const auto& lower = SPEED_CALIBRATION[i];
      const auto& upper = SPEED_CALIBRATION[i+1];
      
      float ratio = static_cast<float>(speed - lower.speed) / 
                    (upper.speed - lower.speed);
      
      //Frequency interpolation
      float interpolated_freq = lower.frequency + ratio * (upper.frequency - lower.frequency);
      
      return calculate_required_coefficient(speed, interpolated_freq);
    }
  }
  return 1.3f;
}

uint Cluster::calculate_freq_from_speed(uint speed) {
  static std::array<float, 3> coeff_buffer = {1.3f, 1.3f, 1.3f};
  static uint8_t buffer_index = 0;
  
  if(speed == 0) return 0;
  
  const float new_coeff = calculate_dynamic_coefficient(speed);
  
  coeff_buffer[buffer_index] = new_coeff;
  buffer_index = (buffer_index + 1) % coeff_buffer.size();
  
  const float smoothed_coeff = 
    (coeff_buffer[0] * 0.6f) + 
    (coeff_buffer[1] * 0.3f) + 
    (coeff_buffer[2] * 0.1f);
  
  float freq = smoothed_coeff * speed + SPEED_OFFSET;
  
  static constexpr std::array<std::pair<uint, float>, 2> hard_calibration = {{
    {60, 80.0f}, {120, 158.1f}
  }};
  
  for(const auto& [s, f] : hard_calibration) {
    if(speed == s) {
      freq = f;
      break;
    }
  }
  
  return static_cast<uint>(freq + 0.5f);
}

uint Cluster::calculate_freq_from_rpm(uint rpm) {
  int freq = int(RPM_COEFFICIENT * float(rpm) + RPM_OFFSET);
  return (freq < 1) ? 1 : freq;
}

Cluster::Cluster() {
  init_output(SPEED_OUTPUT_PIN);
  init_output(TACHO_OUTPUT_PIN);
}

uint32_t Cluster::calculate_period(uint freq) {
  return (1000000 / freq) / 2;
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
      speedometer.alarm = add_alarm_in_us(speedometer.half_period_us, toggle_callback, &speedometer, false);
    break;

    case TACHO_OUTPUT_PIN:
      tachometer.pin = pin;
      tachometer.alarm = add_alarm_in_us(tachometer.half_period_us, toggle_callback, &tachometer, false);
    break;
  }
}

void Cluster::update(uint speed, uint rpm) {
  if(speed < 1) {
    if(speedometer.alarm != -1) {
      cancel_alarm(speedometer.alarm);
      speedometer.alarm = -1;
      gpio_put(speedometer.pin, 0);
    }
  } else {
    calculate_freq_half_period(SPEED_OUTPUT_PIN, speed);
    if(speedometer.alarm == -1) {
      speedometer.alarm = add_alarm_in_us(speedometer.half_period_us, toggle_callback, &speedometer, false);
    }
  }

  if(rpm < 1) {
    if(tachometer.alarm != -1) {
      cancel_alarm(tachometer.alarm);
      tachometer.alarm = -1;
      gpio_put(tachometer.pin, 0);
    }
  } else {
    calculate_freq_half_period(TACHO_OUTPUT_PIN, rpm);
    if(tachometer.alarm == -1) {
      tachometer.alarm = add_alarm_in_us(tachometer.half_period_us, toggle_callback, &tachometer, false);
    }
  }
}