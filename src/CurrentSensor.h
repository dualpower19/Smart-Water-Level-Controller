#pragma once
// Pump current from a current transformer (e.g. SCT-013) on an ESP32 ADC1 pin,
// plus optional mains voltage from a ZMPT101B module.
//
// Both signals are AC riding on a ~1.65 V DC bias. A FreeRTOS task samples
// them over a whole number of mains cycles (10 cycles = 200 ms at 50 Hz),
// removes the DC part and computes true RMS. A bias far from mid-rail means
// the bias network or the input is broken -> reading is marked invalid.
#include <Arduino.h>
#include "Settings.h"

struct ElecReading {
  uint32_t seq = 0;            // increments on every completed sample window
  uint32_t atMs = 0;
  bool  currentValid = false;
  float current = NAN;         // A RMS, after zero/noise correction
  float currentRawA = NAN;     // A RMS before zero correction (for calibration)
  float ctRmsV = NAN;          // RMS volts at the ADC pin (AC part)
  float ctBiasV = NAN;         // DC bias at the ADC pin
  bool  voltageValid = false;
  float voltage = NAN;         // V RMS
  float vtRmsV = NAN, vtBiasV = NAN;
  uint32_t samples = 0;        // per channel, last window
  uint32_t errCount = 0;       // windows with bad bias
};

class CurrentSensor {
 public:
  void begin(const CtConfig& cfg, bool voltageEnabled);
  void setConfig(const CtConfig& cfg, bool voltageEnabled);  // thread-safe
  ElecReading read();                                         // thread-safe snapshot

 private:
  static void taskThunk(void* self);
  void task();

  CtConfig cfg_;
  bool voltageEnabled_ = false;
  ElecReading r_;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};
