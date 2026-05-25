#include <JaszczurHAL.h>

extern "C" void setup_c(void);
extern "C" void loop_c(void);

void setup(void) {
    setup_c();
}

void loop(void) {
    loop_c();
}
