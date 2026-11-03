#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "PumpController.h"

struct TankConfig {
  float emptyDistCm = 200.0f;   // sensor reading (cm) when tank is EMPTY (at the outlet / minimum usable level)
  float fullDistCm  = 35.0f;    // sensor reading (cm) when tank is FULL (keep above the sensor's blind zone)
  float capacityL   = 1000.0f;  // usable volume between empty and full, for the litre readout
};

struct SensorConfig {
  uint8_t  address  = 1;        // Modbus slave id
  uint32_t baud     = 9600;
  uint8_t  function = 3;        // 3 = read holding registers, 4 = read input registers
  uint16_t reg      = 0x0100;   // distance register (check your sensor's datasheet)
  float    unitMm   = 1.0f;     // mm per raw count (1 = mm, 10 = cm, 0.1 = 0.1 mm)
  uint8_t  median   = 5;        // median filter window (1..9)
};

struct CtConfig {
  float   ampsPerVolt  = 30.0f;   // pump amps per ADC volt RMS. SCT-013-030 = 30, SCT-013-000 + 33R burden = 60.6
  float   zeroA        = 0.0f;    // noise floor with pump OFF, removed in quadrature ("Zero CT" button)
  float   minA         = 0.15f;   // readings below this are shown as 0 A
  float   voltsPerVolt = 250.0f;  // ZMPT101B: mains volts per ADC volt RMS (calibrate against a meter)
  uint8_t mainsHz      = 50;
};

struct NetConfig {
  char ssid[33]     = "";
  char pass[65]     = "";
  char hostname[33] = "";
  char adminPass[33] = "";
  char tz[48]       = "IST-5:30";  // POSIX TZ string
};

struct Settings {
  wlc::ControlConfig ctl;
  TankConfig   tank;
  SensorConfig sensor;
  CtConfig     ct;
  NetConfig    net;
  uint8_t      mode = 0;  // wlc::Mode, persisted across reboots
};

namespace SettingsStore {
  void load(Settings& s);
  void save(const Settings& s);
  void factoryReset(Settings& s);
  void validate(Settings& s);

  // JSON (passwords are never exported)
  void toJson(const Settings& s, JsonObject out);
  // Applies any fields present in `in`. Returns true if sensor (UART) params changed.
  bool fromJson(Settings& s, JsonObjectConst in);
}
