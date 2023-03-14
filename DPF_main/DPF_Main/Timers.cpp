#include "Timers.h"


void Timers::restart()
{
  _lastTime = millis();
}

void Timers::begin(const uint32_t interval)
{
  time(interval);
  restart();
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