#ifndef T_CLUSTER
#define T_CLUSTER

#include <tools.h>

#include <algorithm>
#include <array>
#include <cmath>

#include "hardwareConfig.h"
#include <hal/hal.h>

typedef struct {
  volatile uint32_t       half_period_us;
  volatile bool           state;
  unsigned int            pin;
  volatile unsigned int   freq;
  volatile hal_alarm_id_t alarm;
} cluster_s;

class Cluster {
public:
  Cluster();
  void update(unsigned int speed, unsigned int rpm);

private:
  static cluster_s speedometer;
  static cluster_s tachometer;
  void init_output(unsigned int pin);
  uint32_t calculate_period(unsigned int freq);
  unsigned int calculate_freq_from_speed(unsigned int speed);
  unsigned int calculate_freq_from_rpm(unsigned int rpm);
  void calculate_freq_half_period(unsigned int pin, unsigned int value);

  void resetSpeed();
  void resetRPM();

  unsigned int lastSpeed;
  unsigned int lastRpm;
};


#endif
