#include "clusterOutput.h"
#include "Cluster.h"
#include "can.h"
#include "testOutputs.h"

static Cluster s_cluster;

void initClusterOutput(void) { s_cluster.init(); }

void updateCluster(void) {
  unsigned int speed = (unsigned int)canGetValue(F_ABS_CAR_SPEED);
  unsigned int rpm = (unsigned int)canGetValue(F_RPM);
  unsigned int testValue = 0u;
  if (testOutputsGauge(TEST_OUTPUT_SPEED, &testValue)) {
    speed = testValue;
  }
  if (testOutputsGauge(TEST_OUTPUT_RPM, &testValue)) {
    rpm = testValue;
  }
  s_cluster.update(speed, rpm);
}
