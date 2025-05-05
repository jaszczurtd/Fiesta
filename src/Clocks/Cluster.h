#ifndef T_CLUSTER
#define T_CLUSTER

#include <Arduino.h>
#include <tools.h>

#include "hardwareConfig.h"

#include "hardware/timer.h"

typedef struct {
  volatile uint32_t half_period_us;  
  volatile bool state;
  uint pin;
  volatile uint freq;
} cluster_s;

class Cluster {
public:
  Cluster();
  void update(uint speed, uint rpm);

private:
  static cluster_s speedometer;
  static cluster_s tachometer;
  void init_output(uint pin);
  uint32_t calculate_period(uint freq);
  uint calculate_freq_from_speed(uint speed);
  uint calculate_freq_from_rpm(uint rpm);
  void calculate_freq_half_period(uint pin, uint value);
};


#endif
