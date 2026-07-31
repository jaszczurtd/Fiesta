/*
 * Firmware build glue for the shared SC reply helpers.
 *
 * The descriptor-driven SC reply helpers live in src/common/scDefinitions/,
 * outside the module source directory collected by the native firmware
 * build. Including the .c file here makes the implementation visible to this
 * translation unit, matching the source linked explicitly by host CMake.
 *
 * The host CMake build excludes this glue and links the common .c
 * directly via fiesta_sc_definitions / ECU_SOURCES, so each source
 * file is compiled exactly once on either path.
 */

#include "../common/scDefinitions/sc_param_handlers.c"
