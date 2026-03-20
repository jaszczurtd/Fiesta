#ifndef T_HEATED_WINDSHIELD
#define T_HEATED_WINDSHIELD

#include <tools.h>

#include "config.h"
#include "start.h"
#include "tests.h"

class heatedWindshields {
public:
  heatedWindshields();
  void init();  
  void process();
  void showDebug();
  void heatedWindow(bool enable, int side);
  bool isHeatedWindowEnabled(void);
  void heatedWindowMainLoop(void);

private:
  bool heatedWindowEnabled;
  bool lastHeatedWindowEnabled;
  bool waitingForUnpress;

  int heatedWindowsOverallTimer;
  unsigned long lastHeatedWindowsSecond;

  void disableHeatedWindows(void);
  bool isHeatedButtonPressed(void);
};

heatedWindshields *getHeatedWindshieldsInstance(void);
void createHeatedWindshields(void);

#endif