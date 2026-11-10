#ifndef C_CLUSTER_OUTPUT
#define C_CLUSTER_OUTPUT

// The two square waves of the factory gauges, from the measurements or from
// the value of a gauge test.

/** @brief Core 0: set up both gauge outputs and their timers. */
void initClusterOutput(void);
/**
 * @brief Core 0: retune both gauges. Called once per loop pass, from the RPM
 * frame and from the RPM timeout; a gauge under test shows the test value,
 * the other one its measurement.
 */
void updateCluster(void);

#endif
