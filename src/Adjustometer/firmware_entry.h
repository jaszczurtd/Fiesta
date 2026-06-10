#ifndef FIESTA_FIRMWARE_ENTRY_H
#define FIESTA_FIRMWARE_ENTRY_H

/*
 * Entry contract for the CMake-generated application entry point.
 * The generated .ino includes only this file, so module-local headers and
 * core1 enablement stay owned by the module instead of the generator.
 */
#define FIESTA_ENABLE_CORE1 1

#include "start.h"

#endif
