#include "Timers.h"


void Timers::restart()
{
  _lastTime = millis();
}

void Timers::begin(void(*callback)(void), const uint32_t interval)
{
  clb = callback; 
  time(interval);
  restart();
}

void Timers::tick() {
  if(_lastTime == 0 && _time == 0) {
    return;
  }

  if (clb != NULL && available()) {
    clb();
    restart();
  }
}

void Timers::abort() {
  _lastTime = _time = 0;
}

bool Timers::available()
{
  if (_time == 0)
  {
    return false;
  }

  uint32_t actualTime = millis();
  uint32_t deltaTime = actualTime - _lastTime;
  if (deltaTime >= _time)
  {
    return true;
  }

  return false;
}

uint32_t Timers::time()
{
  if (_time == 0)
  {
    return 0;
  }

  uint32_t actualTime = millis();
  uint32_t deltaTime = actualTime - _lastTime;

  return _time - deltaTime;
}

void Timers::time(const uint32_t interval)
{
  _time = interval;
}