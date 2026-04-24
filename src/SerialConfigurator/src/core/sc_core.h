#ifndef SC_CORE_H
#define SC_CORE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SC_MODULE_COUNT 3u
#define SC_PORT_PATH_MAX 512u
#define SC_HELLO_RESPONSE_MAX 512u

typedef struct ScModuleStatus {
    const char *display_name;
    bool detected;
    char port_path[SC_PORT_PATH_MAX];
    char hello_response[SC_HELLO_RESPONSE_MAX];
} ScModuleStatus;

typedef struct ScCore {
    ScModuleStatus modules[SC_MODULE_COUNT];
} ScCore;

void sc_core_init(ScCore *core);
void sc_core_reset_detection(ScCore *core);
void sc_core_detect_modules(ScCore *core, char *log_output, size_t log_output_size);
size_t sc_core_module_count(void);
const ScModuleStatus *sc_core_module_status(const ScCore *core, size_t index);

#ifdef __cplusplus
}
#endif

#endif /* SC_CORE_H */
