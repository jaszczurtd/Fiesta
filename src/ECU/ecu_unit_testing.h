#ifndef ECU_UNIT_TESTING_H
#define ECU_UNIT_TESTING_H

#ifdef UNIT_TEST
#define TESTABLE_STATIC
#define TESTABLE_INLINE_STATIC inline
#else
#define TESTABLE_STATIC static
#define TESTABLE_INLINE_STATIC static inline
#endif

#endif