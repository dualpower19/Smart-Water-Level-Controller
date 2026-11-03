#include "PumpController.h"

namespace wlc {

static constexpr uint32_t SUMP_DEBOUNCE_MS = 3000;

void PumpController::begin(uint32_t nowMs) {
  now_ = nowMs;
  lastStopMs_ = nowMs;   // minimum off time doubles as a power-up restart delay
  lastLevelMs_ = nowMs;  // sensor timeouts are measured from boot until the first reading
  lastCurrentMs_ = nowMs;
  lastVoltMs_ = nowMs;
}

void PumpController::ingest(const Inputs& in) {
  now_ = in.nowMs;
  const uint32_t timeout = (uint32_t)cfg_.sensorTimeoutS * 1000UL;

  if (in.levelValid) { level_ = in.levelPct; lastLevelMs_ = now_; haveLevel_ = true; }
  levelLost_ = (now_ - lastLevelMs_) > timeout;
  levelOk_ = haveLevel_ && !levelLost_;

  if (in.currentValid) { amps_ = in.current; lastCurrentMs_ = now_; haveCurrent_ = true; }
  currentLost_ = (now_ - lastCurrentMs_) > timeout;
  currentOk_ = haveCurrent_ && !currentLost_;

  if (in.voltageValid) { volts_ = in.voltage; lastVoltMs_ = now_; haveVolt_ = true; }
  if (!cfg_.voltageEnabled) lastVoltMs_ = now_;   // don't time out a sensor that isn't fitted
  voltLost_ = (now_ - lastVoltMs_) > timeout;
  voltOk_ = haveVolt_ && !voltLost_;

  sumpEmpty_ = cfg_.sumpFloatEnabled &&
               sumpTimer_.held(!in.sumpHasWater, now_, SUMP_DEBOUNCE_MS);

  // Mains voltage band with hysteresis and trip/restore delays
  if (!cfg_.voltageEnabled) {
    vstate_ = VState::NORMAL;
    vBadTimer_.reset(); vGoodTimer_.reset();
  } else if (voltOk_ && in.voltageValid) {
    const float v = volts_;
    if (vstate_ == VState::NORMAL) {
      const bool over = v > cfg_.overVoltageV;
      const bool under = v < cfg_.underVoltageV;
      if (vBadTimer_.held(over || under, now_, (uint32_t)cfg_.voltageTripS * 1000UL)) {
        vstate_ = over ? VState::OVER : VState::UNDER;
        vBadTimer_.reset(); vGoodTimer_.reset();
      }
    } else {
      const bool inBand = v <= (cfg_.overVoltageV - cfg_.voltageHystV) &&
                          v >= (cfg_.underVoltageV + cfg_.voltageHystV);
      if (vGoodTimer_.held(inBand, now_, (uint32_t)cfg_.voltageRestoreS * 1000UL)) {
        vstate_ = VState::NORMAL;
        vBadTimer_.reset(); vGoodTimer_.reset();
      }
    }
  }
}

bool PumpController::isRetryFault(Fault f) const {
  return f == Fault::DRY_RUN || f == Fault::NO_RISE || f == Fault::MAX_RUNTIME;
}

void PumpController::recoverFaults() {
  if (fault_ == Fault::NONE || latched_) return;
  bool clear = false;
  switch (fault_) {
    case Fault::LEVEL_SENSOR:  clear = levelOk_; break;
    case Fault::OVER_VOLTAGE:
    case Fault::UNDER_VOLTAGE: clear = vstate_ == VState::NORMAL; break;
    case Fault::CURRENT_SENSOR: clear = currentOk_; break;
    case Fault::VOLTAGE_SENSOR: clear = !cfg_.voltageEnabled || voltOk_; break;
    case Fault::DRY_RUN:
    case Fault::NO_RISE:
    case Fault::MAX_RUNTIME:
      clear = (now_ - faultMs_) >= (uint32_t)cfg_.retryDelayMin * 60000UL;
      break;
    default: break;
  }
  if (clear) {
    const Fault f = fault_;
    fault_ = Fault::NONE;
    emit(Event::FAULT_CLEAR, f);
  }
}

void PumpController::trip(Fault f) {
  fault_ = f;
  faultMs_ = now_;
  latched_ = false;
  if (f == Fault::OVERLOAD) {
    latched_ = true;
  } else if (isRetryFault(f)) {
    retries_++;
    if (cfg_.maxRetries == 0 || retries_ > cfg_.maxRetries) latched_ = true;
  }
  if (running_) stop();
  emit(Event::FAULT_TRIP, f);
}

void PumpController::start() {
  running_ = true;
  startMs_ = now_;
  riseWindowMs_ = now_;
  riseRef_ = level_;
  overTimer_.reset();
  dryTimer_.reset();
  startCount_++;
  emit(Event::PUMP_START);
}

void PumpController::stop() {
  if (!running_) return;
  running_ = false;
  lastStopMs_ = now_;
  totalRunMs_ += (now_ - startMs_);
  emit(Event::PUMP_STOP);
}

void PumpController::update(const Inputs& in) {
  ingest(in);
  recoverFaults();

  // ---- Condition faults (checked whether or not the pump is running) ----
  if (fault_ == Fault::NONE) {
    if (levelLost_)                      trip(Fault::LEVEL_SENSOR);
    else if (cfg_.voltageEnabled && voltLost_) trip(Fault::VOLTAGE_SENSOR);
    else if (vstate_ == VState::OVER)    trip(Fault::OVER_VOLTAGE);
    else if (vstate_ == VState::UNDER)   trip(Fault::UNDER_VOLTAGE);
  }

  // ---- Running protections ----
  if (fault_ == Fault::NONE && running_) {
    const uint32_t run = now_ - startMs_;
    if (cfg_.requireCurrentSensor && currentLost_) {
      trip(Fault::CURRENT_SENSOR);
    } else if (currentOk_ && cfg_.overloadEnabled &&
               overTimer_.held(amps_ > cfg_.overloadCurrentA, now_,
                               (uint32_t)cfg_.overloadDelayS * 1000UL)) {
      trip(Fault::OVERLOAD);
    } else if (currentOk_ && cfg_.dryRunEnabled &&
               run >= (uint32_t)cfg_.startGraceS * 1000UL &&
               dryTimer_.held(amps_ < cfg_.underloadCurrentA, now_,
                              (uint32_t)cfg_.dryRunDelayS * 1000UL)) {
      trip(Fault::DRY_RUN);
    } else if (cfg_.maxRunMin > 0 && run >= (uint32_t)cfg_.maxRunMin * 60000UL) {
      trip(Fault::MAX_RUNTIME);
    } else if (cfg_.noRiseEnabled && levelOk_ &&
               (now_ - riseWindowMs_) >= (uint32_t)cfg_.noRiseWindowMin * 60000UL) {
      if (level_ - riseRef_ < cfg_.noRiseMinPct) {
        trip(Fault::NO_RISE);
      } else {
        riseRef_ = level_;
        riseWindowMs_ = now_;
      }
    }
  }

  if (fault_ != Fault::NONE) {
    if (running_) stop();
    hold_ = Hold::NONE;
    return;
  }

  // ---- Fill request latch ----
  if (mode_ == Mode::OFF) {
    fillRequest_ = false;
  } else if (levelOk_) {
    if (level_ >= cfg_.stopLevelPct) {
      if (fillRequest_ && running_) retries_ = 0;  // completed a fill successfully
      fillRequest_ = false;
    } else if (mode_ == Mode::AUTO && level_ <= cfg_.startLevelPct) {
      fillRequest_ = true;
    }
  }

  // ---- Decide relay state ----
  Hold h = Hold::NONE;
  if (mode_ == Mode::OFF)                                   h = Hold::MODE_OFF;
  else if (!levelOk_)                                       h = Hold::WAIT_SENSOR;
  else if (level_ >= cfg_.stopLevelPct)                     h = Hold::TANK_FULL;
  else if (!fillRequest_)                                   h = Hold::IDLE;
  else if (sumpEmpty_)                                      h = Hold::SUMP_EMPTY;
  else if (!running_ && cfg_.requireCurrentSensor && !currentOk_) h = Hold::NO_CURRENT_SENSOR;
  else if (!running_ && minOffRemainingS() > 0)             h = Hold::MIN_OFF_TIME;
  hold_ = h;

  if (h == Hold::NONE && !running_) start();
  else if (h != Hold::NONE && running_) stop();
}

void PumpController::setMode(Mode m) {
  if (m == mode_) return;
  mode_ = m;
  fillRequest_ = false;
  if (running_) { stop(); hold_ = Hold::IDLE; }
  emit(Event::MODE_CHANGE);
}

bool PumpController::requestStart() {
  if (mode_ == Mode::OFF || latched_) return false;
  if (levelOk_ && level_ >= cfg_.stopLevelPct) return false;
  fillRequest_ = true;
  return true;
}

void PumpController::requestStop() {
  fillRequest_ = false;
  if (running_) { stop(); hold_ = Hold::IDLE; }
}

void PumpController::resetFault() {
  retries_ = 0;
  if (fault_ != Fault::NONE) {
    const Fault f = fault_;
    fault_ = Fault::NONE;
    latched_ = false;
    emit(Event::FAULT_CLEAR, f);
  }
}

uint32_t PumpController::retryInS() const {
  if (!isRetryFault(fault_) || latched_) return 0;
  const uint32_t total = (uint32_t)cfg_.retryDelayMin * 60000UL;
  const uint32_t el = now_ - faultMs_;
  return el >= total ? 0 : (total - el + 999) / 1000;
}

uint32_t PumpController::minOffRemainingS() const {
  if (running_) return 0;
  const uint32_t total = (uint32_t)cfg_.minOffS * 1000UL;
  const uint32_t el = now_ - lastStopMs_;
  return el >= total ? 0 : (total - el + 999) / 1000;
}

const char* PumpController::faultName(Fault f) {
  switch (f) {
    case Fault::NONE:          return "None";
    case Fault::OVERLOAD:      return "Overload";
    case Fault::DRY_RUN:       return "Underload/dry run";
    case Fault::NO_RISE:       return "Level not rising";
    case Fault::MAX_RUNTIME:   return "Max run time";
    case Fault::OVER_VOLTAGE:  return "Over voltage";
    case Fault::UNDER_VOLTAGE: return "Under voltage";
    case Fault::LEVEL_SENSOR:  return "Level sensor fault";
    case Fault::CURRENT_SENSOR: return "Current sensor fault";
    case Fault::VOLTAGE_SENSOR: return "Voltage sensor fault";
  }
  return "?";
}

const char* PumpController::holdName(Hold h) {
  switch (h) {
    case Hold::NONE:           return "Running";
    case Hold::MODE_OFF:       return "Mode OFF";
    case Hold::WAIT_SENSOR:    return "Waiting for sensor";
    case Hold::IDLE:           return "Idle";
    case Hold::TANK_FULL:      return "Tank full";
    case Hold::MIN_OFF_TIME:   return "Restart delay";
    case Hold::SUMP_EMPTY:     return "Sump empty";
    case Hold::NO_CURRENT_SENSOR: return "No current sensor";
  }
  return "?";
}

const char* PumpController::modeName(Mode m) {
  switch (m) {
    case Mode::AUTO:   return "AUTO";
    case Mode::MANUAL: return "MANUAL";
    case Mode::OFF:    return "OFF";
  }
  return "?";
}

}  // namespace wlc
