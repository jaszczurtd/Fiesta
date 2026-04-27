/*
 * R1.2 build glue for arduino-cli.
 *
 * arduino-cli only compiles files inside the sketch directory; the
 * descriptor-driven SC reply helpers in src/common/scDefinitions/ are
 * not part of the sketch tree, so the firmware build cannot pick them
 * up directly without bespoke library plumbing. Including the .c file
 * here makes the implementation visible to this translation unit, so
 * the firmware links it the same way the host CMake build links the
 * source path explicitly.
 *
 * The host CMake build excludes this glue and links the common .c
 * directly via fiesta_sc_definitions / ECU_SOURCES, so each source
 * file is compiled exactly once on either path.
 */

#include "../common/scDefinitions/sc_param_handlers.c"
