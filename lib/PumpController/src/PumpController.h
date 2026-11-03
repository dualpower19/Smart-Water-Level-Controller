#pragma once
// ---------------------------------------------------------------------------
// PumpController - hardware-independent pump state machine + protections.
//
// No Arduino dependencies: feed it readings every tick via update() and drive
// the relay from relayOn(). This keeps all safety logic unit-testable on a PC
// (see test/test_pump_controller).
//
// Protections:
//   * Overload        - current above limit for N s        -> latched (manual reset)
//   * Underload/dry   - current below limit after start grace -> auto-retry, then latched
//   * No level rise   - tank level not rising while pumping -> auto-retry, then latched
//   * Max run time    - pump running too long               -> auto-retry, then latched
//   * Over/under volt - mains outside band (optional sensor) -> auto-clears when voltage recovers
//   * Level sensor    - no valid reading for N s            -> auto-clears when sensor returns
//   * Current sensor  - CT input invalid while pump running -> auto-clears when input returns
//   * Voltage sensor  - voltage input invalid (if enabled)  -> auto-clears when input returns
// Plus: anti short-cycle minimum off time (also applied after power-up),
// optional sump float switch, AUTO / MANUAL / OFF modes.
// ---------------------------------------------------------------------------
#include <stdint.h>
#include <functional>

namespace wlc {

enum class Mode : uint8_t { AUTO = 0, MANUAL = 1, OFF = 2 };

enum class Fault : uint8_t {
  NONE = 0,
  OVERLOAD,
  DRY_RUN,
  NO_RISE,
  MAX_RUNTIME,
  OVER_VOLTAGE,
  UNDER_VOLTAGE,
  LEVEL_SENSOR,
  CURRENT_SENSOR,
  VOLTAGE_SENSOR,
};

// Why the pump is not running even though there is no fault.
enum class Hold : uint8_t {
  NONE = 0,       // pump running (or about to)
  MODE_OFF,
  WAIT_SENSOR,    // no level reading yet since boot
  IDLE,           // nothing requested (AUTO: level above start point, MANUAL: not started)
  TANK_FULL,
  MIN_OFF_TIME,   // anti short-cycle / power-up restart delay
  SUMP_EMPTY,
  NO_CURRENT_SENSOR,
};

enum class Event : uint8_t { PUMP_START, PUMP_STOP, FAULT_TRIP, FAULT_CLEAR, MODE_CHANGE };

struct ControlConfig {
  // Level band (percent of usable tank height)
  float startLevelPct = 30.0f;     // AUTO: start filling at or below
  float stopLevelPct  = 95.0f;     // stop at or above (all modes)

  // Overload
  bool     overloadEnabled  = true;
  float    overloadCurrentA = 8.0f;
  uint16_t overloadDelayS   = 3;

  // Underload / dry run (pump running without water draws less current)
  bool     dryRunEnabled      = true;
  float    underloadCurrentA  = 3.0f;
  uint16_t dryRunDelayS   = 10;
  uint16_t startGraceS    = 20;    // ignore underload while the pump primes

  // Level must rise by at least noRiseMinPct within each noRiseWindowMin while pumping
  bool     noRiseEnabled   = true;
  uint16_t noRiseWindowMin = 10;
  float    noRiseMinPct    = 2.0f;

  uint16_t maxRunMin = 60;         // 0 = disabled

  // Mains voltage (needs the optional voltage sensor)
  bool     voltageEnabled   = false;
  float    overVoltageV     = 260.0f;
  float    underVoltageV    = 180.0f;
  float    voltageHystV     = 5.0f;
  uint16_t voltageTripS     = 5;
  uint16_t voltageRestoreS  = 30;

  // Auto-retry for DRY_RUN / NO_RISE / MAX_RUNTIME
  uint16_t retryDelayMin = 30;
  uint8_t  maxRetries    = 3;      // 0 = never auto-retry (latch immediately)

  uint16_t minOffS          = 60;  // anti short-cycle, also restart delay after power-up
  uint16_t sensorTimeoutS   = 10;  // level / current / voltage input considered lost after this
  bool     requireCurrentSensor = true;
  bool     sumpFloatEnabled  = false;
};

struct Inputs {
  uint32_t nowMs = 0;
  bool  levelValid = false;  float levelPct = 0;
  bool  currentValid = false; float current = 0;   // RMS amps
  bool  voltageValid = false; float voltage = 0;   // RMS volts (optional sensor)
  bool  sumpHasWater = true;
};

class PumpController {
 public:
  using EventFn = std::function<void(Event, Fault)>;

  explicit PumpController(const ControlConfig& cfg) : cfg_(cfg) {}

  void begin(uint32_t nowMs);
  void update(const Inputs& in);

  // Commands
  void setMode(Mode m);
  bool requestStart();     // fill to stop level (MANUAL or AUTO). false if refused
  void requestStop();      // cancel current fill request
  void resetFault();       // clear any fault incl. latched ones, zero retry counter

  void setConfig(const ControlConfig& c) { cfg_ = c; }
  const ControlConfig& config() const { return cfg_; }
  void onEvent(EventFn fn) { eventFn_ = fn; }

  // Status
  bool     relayOn() const { return running_; }
  Mode     mode() const { return mode_; }
  Fault    fault() const { return fault_; }
  bool     faultLatched() const { return latched_; }
  Hold     hold() const { return hold_; }
  bool     fillRequested() const { return fillRequest_; }
  bool     levelOk() const { return levelOk_; }
  bool     currentOk() const { return currentOk_; }
  bool     voltageOk() const { return voltOk_; }
  float    levelPct() const { return level_; }
  uint8_t  retries() const { return retries_; }
  uint32_t runTimeS() const { return running_ ? (now_ - startMs_) / 1000 : 0; }
  uint32_t retryInS() const;         // seconds until auto-retry, 0 if n/a
  uint32_t minOffRemainingS() const;
  uint32_t startCount() const { return startCount_; }
  uint32_t totalRunS() const { return totalRunMs_ / 1000 + runTimeS(); }

  static const char* faultName(Fault f);
  static const char* holdName(Hold h);
  static const char* modeName(Mode m);

 private:
  struct Timer {  // "condition held continuously for ms"
    bool on = false; uint32_t since = 0;
    bool held(bool cond, uint32_t now, uint32_t ms) {
      if (!cond) { on = false; return false; }
      if (!on) { on = true; since = now; }
      return (now - since) >= ms;
    }
    void reset() { on = false; }
  };
  enum class VState : uint8_t { NORMAL, OVER, UNDER };  // (HIGH/LOW are Arduino macros)

  void ingest(const Inputs& in);
  void recoverFaults();
  void trip(Fault f);
  void start();
  void stop();
  bool isRetryFault(Fault f) const;
  void emit(Event e, Fault f = Fault::NONE) { if (eventFn_) eventFn_(e, f); }

  ControlConfig cfg_;
  EventFn eventFn_;

  uint32_t now_ = 0;
  Mode  mode_ = Mode::AUTO;
  Fault fault_ = Fault::NONE;
  bool  latched_ = false;
  Hold  hold_ = Hold::IDLE;
  bool  running_ = false;
  bool  fillRequest_ = false;

  // sensor tracking
  bool  levelOk_ = false, currentOk_ = false, voltOk_ = false;
  bool  haveLevel_ = false, haveCurrent_ = false, haveVolt_ = false;
  bool  levelLost_ = false, currentLost_ = false, voltLost_ = false;
  uint32_t lastLevelMs_ = 0, lastCurrentMs_ = 0, lastVoltMs_ = 0;
  float level_ = 0, volts_ = 0, amps_ = 0;
  bool  sumpEmpty_ = false;
  Timer sumpTimer_;

  VState vstate_ = VState::NORMAL;
  Timer vBadTimer_, vGoodTimer_;

  // run tracking
  uint32_t startMs_ = 0, lastStopMs_ = 0, faultMs_ = 0;
  uint32_t riseWindowMs_ = 0; float riseRef_ = 0;
  Timer overTimer_, dryTimer_;
  uint8_t  retries_ = 0;
  uint32_t startCount_ = 0;
  uint64_t totalRunMs_ = 0;
};

}  // namespace wlc
