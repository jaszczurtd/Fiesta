
#ifndef ENGINE_MAPS
#define ENGINE_MAPS

#include <tools_c.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RPM_PRESCALERS 8
#define N75_PERCENT_VALS 10

extern int32_t RPM_table[RPM_PRESCALERS][N75_PERCENT_VALS];

#ifdef __cplusplus
}
#endif

#endif
