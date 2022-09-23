
#include "turbo.h"

static int lastLoad = 0;

void turboMainLoop(void) {

  int load = (int)valueFields[F_ENGINE_LOAD];
  if(load != lastLoad) {
    lastLoad = load;

    valToPWM(10, load);
  }


}