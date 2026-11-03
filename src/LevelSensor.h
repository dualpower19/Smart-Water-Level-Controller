#pragma once
// RS485 (Modbus RTU) ultrasonic level sensor, polled from its own FreeRTOS task
// so a slow/missing sensor (ModbusMaster blocks up to 2 s on timeout) never
// stalls the control loop.
#include <Arduino.h>
#include "Settings.h"

struct LevelReading {
  uint32_t seq = 0;          // increments on every new valid sample
  uint32_t atMs = 0;         // millis() of last valid sample
  uint16_t raw = 0;          // last raw register value
  float    distanceCm = NAN; // filtered distance
  float    levelPct = NAN;   // 0..100
  float    litres = NAN;
  uint32_t okCount = 0, errCount = 0;
  uint8_t  lastError = 0;    // ModbusMaster result code of last failed read (0xE2 = timeout)
};

class LevelSensor {
 public:
  void begin(const SensorConfig& sc, const TankConfig& tc);
  void setConfig(const SensorConfig& sc, const TankConfig& tc);  // thread-safe, applied by the task
  LevelReading read();                                         // thread-safe snapshot

 private:
  static void taskThunk(void* self);
  void task();
  void applyUart();
  float median(float v);

  SensorConfig sc_, pending_;
  TankConfig tc_;
  volatile bool reinit_ = true;
  LevelReading r_;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  float win_[9] = {0};
  uint8_t winCount_ = 0, winPos_ = 0;
};
