#include "LevelSensor.h"
#include <ModbusMaster.h>
#include "config.h"

static ModbusMaster node;

static void preTx()  { if (PIN_RS485_DE >= 0) digitalWrite(PIN_RS485_DE, HIGH); }
static void postTx() { if (PIN_RS485_DE >= 0) digitalWrite(PIN_RS485_DE, LOW); }

void LevelSensor::begin(const SensorConfig& sc, const TankConfig& tc) {
  sc_ = pending_ = sc;
  tc_ = tc;
  if (PIN_RS485_DE >= 0) {
    pinMode(PIN_RS485_DE, OUTPUT);
    digitalWrite(PIN_RS485_DE, LOW);
  }
  node.preTransmission(preTx);
  node.postTransmission(postTx);
  xTaskCreatePinnedToCore(taskThunk, "level", 4096, this, 2, nullptr, 0);
}

void LevelSensor::setConfig(const SensorConfig& sc, const TankConfig& tc) {
  portENTER_CRITICAL(&mux_);
  pending_ = sc;
  tc_ = tc;
  reinit_ = true;
  portEXIT_CRITICAL(&mux_);
}

LevelReading LevelSensor::read() {
  portENTER_CRITICAL(&mux_);
  LevelReading copy = r_;
  portEXIT_CRITICAL(&mux_);
  return copy;
}

void LevelSensor::taskThunk(void* self) { static_cast<LevelSensor*>(self)->task(); }

void LevelSensor::applyUart() {
  portENTER_CRITICAL(&mux_);
  const bool uartChanged = pending_.baud != sc_.baud || pending_.address != sc_.address;
  sc_ = pending_;
  reinit_ = false;
  portEXIT_CRITICAL(&mux_);

  static bool started = false;
  if (!started || uartChanged) {
    if (started) Serial2.end();
    Serial2.begin(sc_.baud, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
    node.begin(sc_.address, Serial2);
    started = true;
  }
  winCount_ = winPos_ = 0;  // restart filter after any config change
}

float LevelSensor::median(float v) {
  const uint8_t n = sc_.median;
  win_[winPos_] = v;
  winPos_ = (winPos_ + 1) % n;
  if (winCount_ < n) winCount_++;
  float tmp[9];
  for (uint8_t i = 0; i < winCount_; i++) tmp[i] = win_[i];
  for (uint8_t i = 1; i < winCount_; i++) {  // insertion sort, n <= 9
    float k = tmp[i]; int j = i - 1;
    while (j >= 0 && tmp[j] > k) { tmp[j + 1] = tmp[j]; j--; }
    tmp[j + 1] = k;
  }
  return tmp[winCount_ / 2];
}

void LevelSensor::task() {
  for (;;) {
    if (reinit_) applyUart();

    const uint8_t res = (sc_.function == 4) ? node.readInputRegisters(sc_.reg, 1)
                                            : node.readHoldingRegisters(sc_.reg, 1);
    if (res == ModbusMaster::ku8MBSuccess) {
      const uint16_t raw = node.getResponseBuffer(0);
      const float mm = raw * sc_.unitMm;
      // 0 / 0xFFFF are "no echo" on most sensors; > 8 m is out of range for a 750 cm unit
      const bool plausible = raw != 0 && raw != 0xFFFF && mm > 0 && mm < 8000.0f;

      portENTER_CRITICAL(&mux_);
      r_.raw = raw;
      if (plausible) {
        const float cm = median(mm / 10.0f);
        const float span = tc_.emptyDistCm - tc_.fullDistCm;
        float pct = span > 0 ? (tc_.emptyDistCm - cm) * 100.0f / span : 0;
        pct = constrain(pct, 0.0f, 100.0f);
        r_.distanceCm = cm;
        r_.levelPct = pct;
        r_.litres = pct / 100.0f * tc_.capacityL;
        r_.atMs = millis();
        r_.seq++;
        r_.okCount++;
      } else {
        r_.errCount++;
        r_.lastError = 0xF0;  // implausible value
      }
      portEXIT_CRITICAL(&mux_);
    } else {
      portENTER_CRITICAL(&mux_);
      r_.errCount++;
      r_.lastError = res;
      portEXIT_CRITICAL(&mux_);
    }
    vTaskDelay(pdMS_TO_TICKS(SENSOR_POLL_MS));
  }
}
