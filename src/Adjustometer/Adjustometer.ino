#include "start.h"

/**
 * @brief Arduino core-0 setup entry point for Adjustometer.
 * @return None.
 */
void setup(void) {
    initialization();
}

/**
 * @brief Arduino core-0 loop entry point for Adjustometer.
 * @return None.
 */
void loop() {
    looper();
}


/**
 * @brief Arduino core-1 setup entry point for Adjustometer.
 * @return None.
 */
void setup1() {
    initialization1();
}

/**
 * @brief Arduino core-1 loop entry point for Adjustometer.
 * @return None.
 */
void loop1() {
    looper1();
}