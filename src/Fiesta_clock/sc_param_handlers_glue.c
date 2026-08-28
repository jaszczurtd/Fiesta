/*
 * Native firmware source bridge for the shared SC command service.
 * A future host build should compile both common sources directly, so each
 * implementation is built exactly once on either path.
 */

#include "../common/scDefinitions/sc_command_handlers.c"
#include "../common/scDefinitions/sc_param_handlers.c"
