# Reads vp37_sources.txt, the single list of VP37 translation units, and sets
#   VP37_SOURCE_DIR - this directory
#   VP37_SOURCES    - the absolute paths of the module's .c files
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/fiesta_shared_sources.cmake")
set(VP37_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")
fiesta_shared_sources(VP37_SOURCES "${FIESTA_SHARED_ROOT}" vp37)
