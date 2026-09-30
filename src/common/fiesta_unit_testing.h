#ifndef FIESTA_UNIT_TESTING_H
#define FIESTA_UNIT_TESTING_H

/* Under UNIT_TEST a TESTABLE_STATIC symbol keeps external linkage, so the
 * host tests can reach it; every firmware build compiles it static. */
#ifdef UNIT_TEST
#define TESTABLE_STATIC
#else
#define TESTABLE_STATIC static
#endif

#endif
