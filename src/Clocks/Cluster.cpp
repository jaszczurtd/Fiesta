
#include "Cluster.h"
#include "timingDiag.h"
#include <hal/core/hal_memory.h>

cluster_s Cluster::speedometer;
cluster_s Cluster::tachometer;

static constexpr float SPEED_OFFSET = 1.70f;
static constexpr float RPM_COEFFICIENT = 0.0332143f;
static constexpr float RPM_OFFSET = 2.14286f;

struct CalibrationPoint {
  uint16_t speed;
  float frequency;
};

constexpr std::array<CalibrationPoint, 13> SPEED_CALIBRATION = {
    {{0, 2.0f},
     {20, 23.5f},
     {30, 36.0f},
     {50, 66.9f},
     {60, 80.0f},
     {80, 106.6f},
     {120, 158.1f},
     {150, 198.2f},
     {180, 236.9f},
     {190, 248.6f},
     {200, 262.9f},
     {210, 275.8f},
     {220, 290.5f}}};

// Timer interrupt: from RAM, so bitmaps read from flash by the display core
// do not delay the gauge edges.
static void HAL_RAM_FUNC(toggle_callback)(hal_timer_t timer, void *user_data) {
  (void)timer;
  cluster_s *data = static_cast<cluster_s *>(user_data);
  if (data != nullptr) {
    hal_gpio_write(data->pin, data->state);
    data->state = !data->state;
    if (data->pin == JH_HW_NODE_TACHO_OUTPUT_PIN_OUT) {
      timingDiagTachoToggle(data->half_period_us);
    }
  }
}

static bool ensure_timer_created(cluster_s *data) {
  if (data == nullptr) {
    return false;
  }
  if (data->timer != nullptr) {
    return true;
  }

  hal_timer_result_t result =
      hal_timer_create(HAL_TIMER_POOL_DEFAULT, data->half_period_us, true,
                       toggle_callback, data, &data->timer);
  return result == HAL_TIMER_OK;
}

static bool apply_period_and_start(cluster_s *data) {
  if (!ensure_timer_created(data)) {
    return false;
  }

  if (hal_timer_set_period_us(data->timer, data->half_period_us, false) !=
      HAL_TIMER_OK) {
    return false;
  }

  if (hal_timer_get_state(data->timer) != HAL_TIMER_STATE_RUNNING) {
    if (hal_timer_start(data->timer) != HAL_TIMER_OK) {
      return false;
    }
  }

  return true;
}

float calculate_required_coefficient(uint16_t speed, float target_freq) {
  return (target_freq - SPEED_OFFSET) / speed;
}

float calculate_dynamic_coefficient(uint16_t speed) {
  for (size_t i = 0; i < SPEED_CALIBRATION.size() - 1; ++i) {
    if (speed >= SPEED_CALIBRATION[i].speed &&
        speed <= SPEED_CALIBRATION[i + 1].speed) {
      const auto &lower = SPEED_CALIBRATION[i];
      const auto &upper = SPEED_CALIBRATION[i + 1];

      float ratio =
          static_cast<float>(speed - lower.speed) / (upper.speed - lower.speed);

      // Frequency interpolation
      float interpolated_freq =
          lower.frequency + ratio * (upper.frequency - lower.frequency);

      return calculate_required_coefficient(speed, interpolated_freq);
    }
  }
  return 1.3f;
}

unsigned int Cluster::calculate_freq_from_speed(unsigned int speed) {
  static std::array<float, 3> coeff_buffer = {1.3f, 1.3f, 1.3f};
  static uint8_t buffer_index = 0;

  if (speed == 0)
    return 0;

  const float new_coeff = calculate_dynamic_coefficient(speed);

  coeff_buffer[buffer_index] = new_coeff;
  buffer_index = (buffer_index + 1) % coeff_buffer.size();

  const float smoothed_coeff = (coeff_buffer[0] * 0.6f) +
                               (coeff_buffer[1] * 0.3f) +
                               (coeff_buffer[2] * 0.1f);

  float freq = smoothed_coeff * speed + SPEED_OFFSET;

  static constexpr std::array<std::pair<unsigned int, float>, 2>
      hard_calibration = {{{60u, 80.0f}, {120u, 158.1f}}};

  for (const auto &[s, f] : hard_calibration) {
    if (speed == s) {
      freq = f;
      break;
    }
  }

  return static_cast<unsigned int>(freq + 0.5f);
}

unsigned int Cluster::calculate_freq_from_rpm(unsigned int rpm) {
  int freq = int(RPM_COEFFICIENT * float(rpm) + RPM_OFFSET);
  return (freq < 1) ? 1 : (unsigned int)freq;
}

Cluster::Cluster() : lastSpeed(0u), lastRpm(0u), initialized(false) {}

// The timer interrupts run on the core that creates the timers, so init()
// is called explicitly on core 0 instead of from a global constructor.
void Cluster::init(void) {
  init_output(JH_HW_NODE_SPEED_OUTPUT_PIN_OUT);
  init_output(JH_HW_NODE_TACHO_OUTPUT_PIN_OUT);
  resetSpeed();
  resetRPM();
  lastSpeed = 0u;
  lastRpm = 0u;
  initialized = true;
}

uint32_t Cluster::calculate_period(unsigned int freq) {
  return (1000000 / freq) / 2;
}

void Cluster::calculate_freq_half_period(unsigned int pin, unsigned int value) {
  switch (pin) {
  case JH_HW_NODE_SPEED_OUTPUT_PIN_OUT:
    speedometer.freq = calculate_freq_from_speed(value);
    speedometer.half_period_us = calculate_period(speedometer.freq);
    break;

  case JH_HW_NODE_TACHO_OUTPUT_PIN_OUT:
    tachometer.freq = calculate_freq_from_rpm(value);
    tachometer.half_period_us = calculate_period(tachometer.freq);
    break;
  }
}

void Cluster::init_output(unsigned int pin) {
  hal_gpio_set_mode(pin, HAL_GPIO_OUTPUT);
  hal_gpio_write(pin, false);

  calculate_freq_half_period(pin, 1);
  switch (pin) {
  case JH_HW_NODE_SPEED_OUTPUT_PIN_OUT:
    speedometer.pin = pin;
    speedometer.state = false;
    speedometer.timer = nullptr;
    (void)ensure_timer_created(&speedometer);
    break;

  case JH_HW_NODE_TACHO_OUTPUT_PIN_OUT:
    tachometer.pin = pin;
    tachometer.state = false;
    tachometer.timer = nullptr;
    (void)ensure_timer_created(&tachometer);
    break;
  }
}

void Cluster::resetSpeed() {
  if (speedometer.timer != nullptr) {
    (void)hal_timer_stop(speedometer.timer);
  }
  speedometer.state = false;
  hal_gpio_write(speedometer.pin, false);
}

void Cluster::resetRPM() {
  if (tachometer.timer != nullptr) {
    (void)hal_timer_stop(tachometer.timer);
  }
  tachometer.state = false;
  hal_gpio_write(tachometer.pin, false);
}

void Cluster::update(unsigned int speed, unsigned int rpm) {
  if (!initialized) {
    return;
  }
  if (lastSpeed != speed) {
    lastSpeed = speed;

    if (speed < 1) {
      resetSpeed();
    } else {
      calculate_freq_half_period(JH_HW_NODE_SPEED_OUTPUT_PIN_OUT, speed);
      if (!apply_period_and_start(&speedometer)) {
        resetSpeed();
      }
    }
  }

  if (lastRpm != rpm) {
    lastRpm = rpm;

    if (rpm < 1) {
      resetRPM();
    } else {
      calculate_freq_half_period(JH_HW_NODE_TACHO_OUTPUT_PIN_OUT, rpm);
      if (!apply_period_and_start(&tachometer)) {
        resetRPM();
      }
    }
  }
}
