#ifndef ECU_UNIT_TESTING_H
#define ECU_UNIT_TESTING_H

#ifdef UNIT_TEST
#define TESTABLE_STATIC
/* Bare `inline` (no static/extern) leaves the symbol with no external
 * definition in C11, which fails to link when the compiler does not
 * inline the call (e.g. -O0). Drop the qualifier under UNIT_TEST so
 * the function is just a regular external-linkage definition. */
#define TESTABLE_INLINE_STATIC
#else
#define TESTABLE_STATIC static
#define TESTABLE_INLINE_STATIC static inline
#endif

#endif