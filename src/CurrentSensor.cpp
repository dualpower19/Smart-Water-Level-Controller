#include "CurrentSensor.h"
#include <math.h>
#include "config.h"

// Bias must sit near mid-rail (1.65 V). Outside this window the divider/cap is
// missing or the input is shorted, so the RMS value means nothing.
static constexpr int BIAS_MIN_MV = 900;
static constexpr int BIAS_MAX_MV = 2400;

void CurrentSensor::begin(const CtConfig& cfg, bool voltageEnabled) {
  cfg_ = cfg;
  voltageEnabled_ = voltageEnabled;
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_CT_ADC, ADC_11db);
  if (PIN_VT_ADC >= 0) analogSetPinAttenuation(PIN_VT_ADC, ADC_11db);
  // Priority 1 on core 0: below the level-sensor task, never competes with loop() on core 1.
  xTaskCreatePinnedToCore(taskThunk, "ct", 4096, this, 1, nullptr, 0);
}

void CurrentSensor::setConfig(const CtConfig& cfg, bool voltageEnabled) {
  portENTER_CRITICAL(&mux_);
  cfg_ = cfg;
  voltageEnabled_ = voltageEnabled;
  portEXIT_CRITICAL(&mux_);
}

ElecReading CurrentSensor::read() {
  portENTER_CRITICAL(&mux_);
  ElecReading copy = r_;
  portEXIT_CRITICAL(&mux_);
  return copy;
}

void CurrentSensor::taskThunk(void* self) { static_cast<CurrentSensor*>(self)->task(); }

void CurrentSensor::task() {
  for (;;) {
    portENTER_CRITICAL(&mux_);
    const CtConfig cfg = cfg_;
    const bool doV = voltageEnabled_ && PIN_VT_ADC >= 0;
    portEXIT_CRITICAL(&mux_);

    // Sample a whole number of mains cycles so the mean removes DC exactly.
    const uint32_t windowUs = 10UL * 1000000UL / cfg.mainsHz;
    double si = 0, sii = 0, sv = 0, svv = 0;
    uint32_t n = 0;
    const uint32_t t0 = micros();
    while (micros() - t0 < windowUs) {
      const double mi = analogReadMilliVolts(PIN_CT_ADC);
      si += mi; sii += mi * mi;
      if (doV) {
        const double mv = analogReadMilliVolts(PIN_VT_ADC);
        sv += mv; svv += mv * mv;
      }
      n++;
    }

    ElecReading out;
    out.samples = n;
    if (n > 50) {
      const double mi = si / n;
      const double ctRms = sqrt(fmax(0.0, sii / n - mi * mi)) / 1000.0;  // volts
      out.ctBiasV = mi / 1000.0;
      out.ctRmsV = ctRms;
      out.currentValid = mi > BIAS_MIN_MV && mi < BIAS_MAX_MV;
      const float raw = ctRms * cfg.ampsPerVolt;
      out.currentRawA = raw;
      float a = sqrtf(fmaxf(0.0f, raw * raw - cfg.zeroA * cfg.zeroA));
      if (a < cfg.minA) a = 0.0f;
      out.current = a;

      if (doV) {
        const double mv = sv / n;
        const double vtRms = sqrt(fmax(0.0, svv / n - mv * mv)) / 1000.0;
        out.vtBiasV = mv / 1000.0;
        out.vtRmsV = vtRms;
        out.voltageValid = mv > BIAS_MIN_MV && mv < BIAS_MAX_MV;
        out.voltage = vtRms * cfg.voltsPerVolt;
      }
    }

    portENTER_CRITICAL(&mux_);
    out.seq = r_.seq + 1;
    out.atMs = millis();
    out.errCount = r_.errCount + (out.currentValid ? 0 : 1);
    r_ = out;
    portEXIT_CRITICAL(&mux_);

    vTaskDelay(pdMS_TO_TICKS(CT_POLL_MS));
  }
}
