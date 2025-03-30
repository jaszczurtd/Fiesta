#ifndef T_CLUSTER
#define T_CLUSTER

#include <Arduino.h>
#include <tools.h>

#include "hardwareConfig.h"

#include "hardware/timer.h"

typedef struct {
  volatile uint32_t half_period_us;  
  bool state;
  uint pin;
  uint freq;
} cluster_s;

class Cluster {
public:
  Cluster();
  void update(int speed, int rpm);

private:
  static cluster_s speedometer;
  static cluster_s tachometer;
  void init_output(uint pin, int value);
  uint32_t calculate_period(uint freq);
  uint calculate_freq_from_speed(float speed);
  uint calculate_freq_from_rpm(float rpm);
};


#endif
