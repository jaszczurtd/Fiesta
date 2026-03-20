#ifndef T_HEATER
#define T_HEATER

#include <tools.h>

#include "config.h"
#include "start.h"
#include "sensors.h"
#include "tests.h"
#include "engineFan.h"

class engineHeater {
public:
  engineHeater();
  void init();  
  void process();
  void showDebug();
  void heater(bool enable, int level);

private:
  bool heaterLoEnabled;
  bool heaterHiEnabled;
  bool lastHeaterLoEnabled;
  bool lastHeaterHiEnabled;

};

engineHeater *getHeaterInstance(void);
void createHeater(void);


#endif