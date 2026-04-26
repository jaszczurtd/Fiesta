#ifndef SC_TRANSPORT_H
#define SC_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SC_TRANSPORT_MAX_CANDIDATES 64u
#define SC_TRANSPORT_PATH_MAX 512u
#define SC_TRANSPORT_RESPONSE_MAX 512u

typedef struct ScTransportCandidateList {
    size_t count;
    bool truncated;
    char paths[SC_TRANSPORT_MAX_CANDIDATES][SC_TRANSPORT_PATH_MAX];
} ScTransportCandidateList;

typedef struct ScTransportOps {
    bool (*list_candidates)(
        void *context,
        ScTransportCandidateList *list,
        char *error,
        size_t error_size
    );
    bool (*resolve_device_path)(
        void *context,
        const char *candidate_path,
        char *device_path,
        size_t device_path_size,
        char *error,
        size_t error_size
    );
    bool (*send_hello)(
        void *context,
        const char *device_path,
        char *response,
        size_t response_size,
        char *error,
        size_t error_size
    );
    bool (*send_sc_command)(
        void *context,
        const char *device_path,
        const char *command,
        char *response,
        size_t response_size,
        char *error,
        size_t error_size
    );
} ScTransportOps;

typedef struct ScTransport {
    const ScTransportOps *ops;
    void *context;
} ScTransport;

void sc_transport_init_default(ScTransport *transport);
void sc_transport_init_custom(ScTransport *transport, const ScTransportOps *ops, void *context);

bool sc_transport_list_candidates(
    const ScTransport *transport,
    ScTransportCandidateList *list,
    char *error,
    size_t error_size
);
bool sc_transport_resolve_device_path(
    const ScTransport *transport,
    const char *candidate_path,
    char *device_path,
    size_t device_path_size,
    char *error,
    size_t error_size
);
bool sc_transport_send_hello(
    const ScTransport *transport,
    const char *device_path,
    char *response,
    size_t response_size,
    char *error,
    size_t error_size
);
bool sc_transport_send_sc_command(
    const ScTransport *transport,
    const char *device_path,
    const char *command,
    char *response,
    size_t response_size,
    char *error,
    size_t error_size
);

#ifdef __cplusplus
}
#endif

#endif /* SC_TRANSPORT_H */
