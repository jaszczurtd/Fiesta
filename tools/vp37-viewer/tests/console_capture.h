#ifndef VP37V_CONSOLE_CAPTURE_TEST_H
#define VP37V_CONSOLE_CAPTURE_TEST_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
bool vp37v_test_console_capture(void);
bool vp37v_test_sample_clock(void);
bool vp37v_test_monitor_help(const char *output_path, const char *log_path,
                             const char *port);
int vp37v_monitor_fixture(int argc, char **argv);
#ifdef __cplusplus
}
#endif
#endif
