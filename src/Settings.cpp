#include "Settings.h"
#include <Preferences.h>
#include "config.h"

// Bump when the Settings layout changes; a mismatch loads factory defaults.
static constexpr uint32_t SETTINGS_MAGIC = 0x574C4302;  // "WLC" v2 (CT current input)

// ---- field lists (name in JSON == member name) ------------------------------
#define CTL_FIELDS(X)                                                        \
  X(startLevelPct) X(stopLevelPct)                                           \
  X(overloadEnabled) X(overloadCurrentA) X(overloadDelayS)                   \
  X(dryRunEnabled) X(underloadCurrentA) X(dryRunDelayS) X(startGraceS)            \
  X(noRiseEnabled) X(noRiseWindowMin) X(noRiseMinPct) X(maxRunMin)           \
  X(voltageEnabled) X(overVoltageV) X(underVoltageV) X(voltageHystV)         \
  X(voltageTripS) X(voltageRestoreS)                                         \
  X(retryDelayMin) X(maxRetries) X(minOffS) X(sensorTimeoutS)                \
  X(requireCurrentSensor) X(sumpFloatEnabled)

#define TANK_FIELDS(X)   X(emptyDistCm) X(fullDistCm) X(capacityL)
#define SENSOR_FIELDS(X) X(address) X(baud) X(function) X(reg) X(unitMm) X(median)
#define CT_FIELDS(X)     X(ampsPerVolt) X(zeroA) X(minA) X(voltsPerVolt) X(mainsHz)

namespace {
struct Blob {
  uint32_t magic;
  uint32_t size;
  Settings s;
};

template <typename T> T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

namespace SettingsStore {

void validate(Settings& s) {
  auto& c = s.ctl;
  c.startLevelPct = clampv(c.startLevelPct, 0.0f, 95.0f);
  c.stopLevelPct  = clampv(c.stopLevelPct, 5.0f, 100.0f);
  if (c.stopLevelPct < c.startLevelPct + 5.0f) c.stopLevelPct = min(100.0f, c.startLevelPct + 5.0f);
  c.overloadCurrentA = clampv(c.overloadCurrentA, 0.1f, 100.0f);
  c.overloadDelayS   = clampv<uint16_t>(c.overloadDelayS, 1, 60);
  c.underloadCurrentA = clampv(c.underloadCurrentA, 0.0f, c.overloadCurrentA * 0.95f);
  c.dryRunDelayS     = clampv<uint16_t>(c.dryRunDelayS, 1, 300);
  c.startGraceS      = clampv<uint16_t>(c.startGraceS, 0, 600);
  c.noRiseWindowMin  = clampv<uint16_t>(c.noRiseWindowMin, 1, 240);
  c.noRiseMinPct     = clampv(c.noRiseMinPct, 0.1f, 50.0f);
  c.maxRunMin        = clampv<uint16_t>(c.maxRunMin, 0, 1440);
  c.overVoltageV     = clampv(c.overVoltageV, 100.0f, 300.0f);
  c.underVoltageV    = clampv(c.underVoltageV, 80.0f, c.overVoltageV - 20.0f);
  c.voltageHystV     = clampv(c.voltageHystV, 0.0f, 20.0f);
  c.voltageTripS     = clampv<uint16_t>(c.voltageTripS, 1, 120);
  c.voltageRestoreS  = clampv<uint16_t>(c.voltageRestoreS, 1, 1800);
  c.retryDelayMin    = clampv<uint16_t>(c.retryDelayMin, 1, 1440);
  c.maxRetries       = clampv<uint8_t>(c.maxRetries, 0, 20);
  c.minOffS          = clampv<uint16_t>(c.minOffS, 5, 3600);
  c.sensorTimeoutS   = clampv<uint16_t>(c.sensorTimeoutS, 3, 120);

  auto& t = s.tank;
  t.fullDistCm  = clampv(t.fullDistCm, 0.0f, 750.0f);
  t.emptyDistCm = clampv(t.emptyDistCm, t.fullDistCm + 5.0f, 800.0f);
  t.capacityL   = clampv(t.capacityL, 0.0f, 1000000.0f);

  auto& n = s.sensor;
  n.address  = clampv<uint8_t>(n.address, 1, 247);
  if (n.baud != 2400 && n.baud != 4800 && n.baud != 9600 && n.baud != 19200 &&
      n.baud != 38400 && n.baud != 57600 && n.baud != 115200) n.baud = 9600;
  if (n.function != 3 && n.function != 4) n.function = 3;
  n.unitMm = clampv(n.unitMm, 0.01f, 100.0f);
  n.median = clampv<uint8_t>(n.median, 1, 9);

  auto& k = s.ct;
  k.ampsPerVolt  = clampv(k.ampsPerVolt, 0.1f, 2000.0f);
  k.zeroA        = clampv(k.zeroA, 0.0f, 5.0f);
  k.minA         = clampv(k.minA, 0.0f, 5.0f);
  k.voltsPerVolt = clampv(k.voltsPerVolt, 1.0f, 5000.0f);
  if (k.mainsHz != 50 && k.mainsHz != 60) k.mainsHz = 50;

  if (s.mode > 2) s.mode = 0;
  if (!s.net.hostname[0]) strlcpy(s.net.hostname, DEFAULT_HOSTNAME, sizeof(s.net.hostname));
  if (!s.net.adminPass[0]) strlcpy(s.net.adminPass, DEFAULT_ADMIN_PASSWORD, sizeof(s.net.adminPass));
  if (!s.net.tz[0]) strlcpy(s.net.tz, "IST-5:30", sizeof(s.net.tz));
}

void factoryReset(Settings& s) {
  s = Settings();
  validate(s);
}

void load(Settings& s) {
  Preferences p;
  p.begin("wlc", true);
  Blob b;
  size_t n = p.getBytes("cfg", &b, sizeof(b));
  p.end();
  if (n == sizeof(b) && b.magic == SETTINGS_MAGIC && b.size == sizeof(Settings)) {
    s = b.s;
  } else {
    s = Settings();
  }
  validate(s);
}

void save(const Settings& s) {
  Blob b;
  b.magic = SETTINGS_MAGIC;
  b.size = sizeof(Settings);
  b.s = s;
  Preferences p;
  p.begin("wlc", false);
  p.putBytes("cfg", &b, sizeof(b));
  p.end();
}

void toJson(const Settings& s, JsonObject out) {
  JsonObject c = out["ctl"].to<JsonObject>();
#define X(f) c[#f] = s.ctl.f;
  CTL_FIELDS(X)
#undef X
  JsonObject t = out["tank"].to<JsonObject>();
#define X(f) t[#f] = s.tank.f;
  TANK_FIELDS(X)
#undef X
  JsonObject n = out["sensor"].to<JsonObject>();
#define X(f) n[#f] = s.sensor.f;
  SENSOR_FIELDS(X)
#undef X
  JsonObject k = out["ct"].to<JsonObject>();
#define X(f) k[#f] = s.ct.f;
  CT_FIELDS(X)
#undef X
  JsonObject w = out["net"].to<JsonObject>();
  w["ssid"] = s.net.ssid;
  w["hostname"] = s.net.hostname;
  w["tz"] = s.net.tz;
}

bool fromJson(Settings& s, JsonObjectConst in) {
  const SensorConfig before = s.sensor;

  JsonObjectConst c = in["ctl"];
  if (c) {
#define X(f) if (!c[#f].isNull()) s.ctl.f = c[#f].as<decltype(s.ctl.f)>();
    CTL_FIELDS(X)
#undef X
  }
  JsonObjectConst t = in["tank"];
  if (t) {
#define X(f) if (!t[#f].isNull()) s.tank.f = t[#f].as<decltype(s.tank.f)>();
    TANK_FIELDS(X)
#undef X
  }
  JsonObjectConst n = in["sensor"];
  if (n) {
#define X(f) if (!n[#f].isNull()) s.sensor.f = n[#f].as<decltype(s.sensor.f)>();
    SENSOR_FIELDS(X)
#undef X
  }
  JsonObjectConst k = in["ct"];
  if (k) {
#define X(f) if (!k[#f].isNull()) s.ct.f = k[#f].as<decltype(s.ct.f)>();
    CT_FIELDS(X)
#undef X
  }
  JsonObjectConst w = in["net"];
  if (w) {
    if (w["ssid"].is<const char*>())     strlcpy(s.net.ssid, w["ssid"], sizeof(s.net.ssid));
    if (w["pass"].is<const char*>())     strlcpy(s.net.pass, w["pass"], sizeof(s.net.pass));
    if (w["hostname"].is<const char*>()) strlcpy(s.net.hostname, w["hostname"], sizeof(s.net.hostname));
    if (w["tz"].is<const char*>())       strlcpy(s.net.tz, w["tz"], sizeof(s.net.tz));
    const char* ap = w["adminPass"] | "";
    if (strlen(ap) >= 4) strlcpy(s.net.adminPass, ap, sizeof(s.net.adminPass));
  }
  validate(s);
  return before.address != s.sensor.address || before.baud != s.sensor.baud;
}

}  // namespace SettingsStore
