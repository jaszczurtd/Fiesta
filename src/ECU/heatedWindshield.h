#ifndef T_HEATED_WINDSHIELD
#define T_HEATED_WINDSHIELD

#include <tools.h>

#include "config.h"
#include "start.h"
#include "tests.h"

#include "EngineController.h"

class heatedWindshields : public EngineController {
public:
  heatedWindshields();
  void init() override;  
  void process() override;
  void showDebug() override;
  void heatedWindow(bool enable, int side);
  bool isHeatedWindowEnabled(void);
  void heatedWindowMainLoop(void);

private:
  bool heatedWindowEnabled = false;
  bool lastHeatedWindowEnabled = false;
  bool waitingForUnpress = false;

  int heatedWindowsOverallTimer = 0;
  unsigned long lastHeatedWindowsSecond = 0;

  void disableHeatedWindows(void);
  bool isHeatedButtonPressed(void);
};

heatedWindshields *getHeatedWindshieldsInstance(void);
void createHeatedWindshields(void);

#endif