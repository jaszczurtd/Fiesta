#ifndef SC_MOCK_ECU_H
#define SC_MOCK_ECU_H

/*
 * Mock module for host tests: a transport that answers HELLO, the
 * authentication handshake and the SC_TEST_* commands the way the
 * firmware does (sc_command_handlers.c), with switches for the refusals.
 * It answers as the ECU, or as Clocks when @c clocks is set: five cluster
 * tests that share the step_ms parameter and report no actuator.
 * The handshake itself is exercised in test_sc_phase8_host.c; here any
 * SC_AUTH_PROVE is accepted.
 */

#include <stdbool.h>
#include <stdint.h>

#include "sc_core.h"
#include "sc_transport.h"

#define MOCK_PATH "/dev/mock/ttyACM0"

typedef struct {
  bool clocks;         /* true: answer as Clocks instead of the ECU. */
  bool tests_compiled; /* false: firmware without SC_TEST_* at all. */
  bool production;     /* true: commands present, empty catalog. */
  bool authenticated;
  bool engine_running;
  unsigned busy_replies; /* BUSY answers before a request is taken. */
  int32_t cyclic_passes;
  int32_t step_ms; /* Clocks: the parameter all tests share. */
  char last_run[32];
  char last_set[64];
  unsigned sets; /* accepted SC_TEST_SET commands */
  unsigned stops;
  unsigned skips;
  unsigned sent;
  const char *status_reply;
} MockEcu;

/** State of the one mock ECU; reset by sc_mock_ecu_reset(). */
extern MockEcu s_ecu;

void sc_mock_ecu_reset(void);

/** Transport operations that talk to the mock ECU. */
extern const ScTransportOps k_mock_ecu_ops;

/** Module index of the mock: the ECU, or Clocks when @c clocks is set. */
size_t sc_mock_ecu_module_index(void);

/** Core with the mock detected at sc_mock_ecu_module_index(). Set @c clocks
 *  before the call. */
bool sc_mock_ecu_detected_core(ScCore *core);

#endif /* SC_MOCK_ECU_H */
