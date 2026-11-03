#pragma once
// PZEM-004T v3.0 energy meter on UART1. The CT clamp goes around ONE wire
// (phase) of the pump motor feed, after the contactor.
#include <Arduino.h>

struct PowerReading {
  uint32_t seq = 0;       // increments on every successful read
  uint32_t atMs = 0;
  float voltage = NAN, current = NAN, power = NAN, energyKWh = NAN, frequency = NAN, pf = NAN;
  uint32_t okCount = 0, errCount = 0;
};

class PowerMonitor {
 public:
  void begin();
  void poll();             // call from loop(); rate-limited internally
  PowerReading read() const { return r_; }
  bool resetEnergy();

 private:
  PowerReading r_;
  uint32_t lastPoll_ = 0;
};
