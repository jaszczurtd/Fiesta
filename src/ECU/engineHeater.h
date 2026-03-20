#ifndef T_HEATER
#define T_HEATER

#include <tools.h>

#include "config.h"
#include "start.h"
#include "sensors.h"
#include "tests.h"
#include "engineFan.h"

#include "EngineController.h"

class engineHeater : public EngineController {
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