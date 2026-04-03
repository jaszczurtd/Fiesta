#ifndef T_HEATED_WINDSHIELD
#define T_HEATED_WINDSHIELD

#include <tools.h>

#include "config.h"
#include "start.h"
#include "tests.h"

typedef struct {
  bool heatedWindowEnabled;
  bool lastHeatedWindowEnabled;
  bool waitingForUnpress;

  int heatedWindowsOverallTimer;
  unsigned long lastHeatedWindowsSecond;
} heatedWindshields;

void heatedWindshields_init(heatedWindshields *self);
void heatedWindshields_process(heatedWindshields *self);
void heatedWindshields_showDebug(heatedWindshields *self);
void heatedWindshields_heatedWindow(heatedWindshields *self, bool enable, int side);
bool heatedWindshields_isHeatedWindowEnabled(heatedWindshields *self);

heatedWindshields *getHeatedWindshieldsInstance(void);
void createHeatedWindshields(void);

#endif
