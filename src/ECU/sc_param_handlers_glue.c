/*
 * Native firmware source bridge for the shared SC command service.
 *
 * The shared implementations live in src/common/scDefinitions/, outside the
 * module source directory collected by the native firmware build. Including
 * them here matches the sources linked explicitly by host CMake.
 *
 * The host CMake build excludes this bridge and links both common sources
 * directly, so each implementation is compiled exactly once on either path.
 */

#include "../common/scDefinitions/sc_command_handlers.c"
#include "../common/scDefinitions/sc_param_handlers.c"
