#include "vp37_internal.h"
#include <string.h>

void VP37_recordDiagnostic(VP37Pump *self, VP37DiagnosticCode code,
                           uint32_t value, uint32_t detail) {
  VP37Diagnostics *const diagnostics = &self->diagnostics;
  if (!HAL_ATOMIC_LOAD(&diagnostics->ready, HAL_ATOMIC_RELAXED)) {
    (void)hal_debug_events_init(&diagnostics->queue, diagnostics->records,
                                sizeof(diagnostics->records[0]),
                                COUNTOF(diagnostics->records));
    HAL_ATOMIC_STORE(&diagnostics->ready, true, HAL_ATOMIC_RELEASE);
  }
  const VP37DiagnosticEvent event = {.code = (uint32_t)code,
                                     .timeUs = hal_micros(),
                                     .value = value,
                                     .detail = detail};
  (void)hal_debug_events_publish(&diagnostics->queue, &event);
}

static hal_status_t printDiagnostic(const void *record, void *context) {
  (void)context;
  VP37DiagnosticEvent event;
  (void)memcpy(&event, record, sizeof(event));
  const unsigned long timeUs = (unsigned long)event.timeUs;
  switch ((VP37DiagnosticCode)event.code) {
  case VP37_DIAG_ENABLED:
    deb("vp37 enable requested: %u us:%lu", (unsigned)event.value, timeUs);
    break;
  case VP37_DIAG_COMM_TIMEOUT:
    derr("VP37 disabled: feedback communication timeout us:%lu", timeUs);
    break;
  case VP37_DIAG_INVALID_FEEDBACK:
    derr("VP37 disabled: invalid feedback status:%u fresh:%u us:%lu",
         (unsigned)event.value, (unsigned)event.detail, timeUs);
    break;
  case VP37_DIAG_DRIVE_FORBIDDEN:
    derr("VP37 disabled: drive not allowed us:%lu", timeUs);
    break;
  case VP37_DIAG_PID_FAILURE:
    derr("VP37 PID step failed: %s us:%lu",
         hal_status_to_string((hal_status_t)event.value), timeUs);
    break;
  default:
    break;
  }
  return HAL_OK;
}

void VP37_printEvents(VP37Pump *self) {
  if ((self == NULL) || hal_debug_is_muted() ||
      !HAL_ATOMIC_LOAD(&self->diagnostics.ready, HAL_ATOMIC_ACQUIRE)) {
    return;
  }
  VP37Diagnostics *const diagnostics = &self->diagnostics;
  (void)hal_debug_events_poll(&diagnostics->queue,
                              COUNTOF(diagnostics->records), printDiagnostic,
                              NULL);
  const uint32_t total = hal_debug_events_dropped(&diagnostics->queue);
  const uint32_t dropped = total - diagnostics->reportedDrops;
  diagnostics->reportedDrops = total;
  if (dropped != 0U) {
    derr("VP37 diagnostic log overflow: %lu", (unsigned long)dropped);
  }
}
