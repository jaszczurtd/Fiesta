#include "engineFuel.h"

//-------------------------------------------------------------------------------------------------
//Read fuel amount
//-------------------------------------------------------------------------------------------------

float readFuel(void) {
    set4051ActivePin(4);

    int result = getAverageValueFrom(A1);

    //todo: macro
    #ifdef DEBUG
    deb("tank: %d", result);
    #endif

    result -= FUEL_MAX;
    result = abs(result - (FUEL_MIN - FUEL_MAX));

    return result;
}

