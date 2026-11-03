// Host-side tests for the pump state machine:  pio test -e native
#include <unity.h>
#include "PumpController.h"

using namespace wlc;

// Small simulation harness: steps time and feeds readings.
struct Sim {
  ControlConfig cfg;
  PumpController* pc = nullptr;
  uint32_t now;
  float level = 50, volts = 230, amps = 5, watts = 900;
  bool levelValid = true, powerValid = true, sump = true;

  explicit Sim(uint32_t start = 1000) : now(start) {
    cfg.minOffS = 5;
    cfg.startGraceS = 10;
    cfg.dryRunDelayS = 5;
    cfg.overloadDelayS = 3;
    cfg.retryDelayMin = 1;
    cfg.maxRetries = 2;
    cfg.noRiseEnabled = false;
    cfg.maxRunMin = 0;
  }
  ~Sim() { delete pc; }
  void begin() { pc = new PumpController(cfg); pc->begin(now); }
  void step(uint32_t ms = 200) {
    now += ms;
    Inputs in;
    in.nowMs = now;
    in.levelValid = levelValid; in.levelPct = level;
    in.powerValid = powerValid; in.voltage = volts; in.current = amps; in.power = watts;
    in.sumpHasWater = sump;
    pc->update(in);
  }
  void run(uint32_t ms) { for (uint32_t t = 0; t < ms; t += 200) step(200); }
  // While pumping, draw normal load; while off, draw nothing.
  void runLoaded(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 200) {
      amps = pc->relayOn() ? 5.0f : 0.0f;
      watts = pc->relayOn() ? 900.0f : 0.0f;
      step(200);
    }
  }
};

void setUp() {}
void tearDown() {}

void test_powerup_delay_then_auto_start_and_stop_at_full() {
  Sim s; s.level = 20; s.begin();
  s.run(2000);
  TEST_ASSERT_FALSE(s.pc->relayOn());
  TEST_ASSERT_EQUAL(Hold::MIN_OFF_TIME, s.pc->hold());
  s.runLoaded(4000);
  TEST_ASSERT_TRUE(s.pc->relayOn());
  s.level = 96; s.runLoaded(400);
  TEST_ASSERT_FALSE(s.pc->relayOn());
  TEST_ASSERT_EQUAL(Hold::TANK_FULL, s.pc->hold());
}

void test_hysteresis_no_start_between_band() {
  Sim s; s.level = 60; s.begin();
  s.run(10000);
  TEST_ASSERT_FALSE(s.pc->relayOn());
  TEST_ASSERT_EQUAL(Hold::IDLE, s.pc->hold());
  s.level = 29; s.runLoaded(400);
  TEST_ASSERT_TRUE(s.pc->relayOn());
  s.level = 60; s.runLoaded(2000);  // keeps filling until stop level
  TEST_ASSERT_TRUE(s.pc->relayOn());
}

void test_overload_latches_and_needs_reset() {
  Sim s; s.level = 10; s.begin();
  s.runLoaded(6000);
  TEST_ASSERT_TRUE(s.pc->relayOn());
  s.amps = 12; s.run(3400);
  TEST_ASSERT_FALSE(s.pc->relayOn());
  TEST_ASSERT_EQUAL(Fault::OVERLOAD, s.pc->fault());
  TEST_ASSERT_TRUE(s.pc->faultLatched());
  s.amps = 0; s.watts = 0; s.run(120000);
  TEST_ASSERT_EQUAL(Fault::OVERLOAD, s.pc->fault());
  TEST_ASSERT_FALSE(s.pc->requestStart());
  s.pc->resetFault();
  s.runLoaded(6000);
  TEST_ASSERT_TRUE(s.pc->relayOn());
}

void test_short_inrush_does_not_trip_overload() {
  Sim s; s.level = 10; s.begin();
  s.runLoaded(6000);
  TEST_ASSERT_TRUE(s.pc->relayOn());
  s.amps = 25; s.run(1000);   // 1 s inrush spike < 3 s delay
  s.amps = 5;  s.run(5000);
  TEST_ASSERT_TRUE(s.pc->relayOn());
  TEST_ASSERT_EQUAL(Fault::NONE, s.pc->fault());
}

void test_dry_run_ignored_during_grace_then_trips() {
  Sim s; s.level = 10; s.begin();
  s.runLoaded(6000);
  TEST_ASSERT_TRUE(s.pc->relayOn());
  s.watts = 100; s.amps = 1;   // dry from the start
  s.run(9000);                 // still within 10 s grace
  TEST_ASSERT_TRUE(s.pc->relayOn());
  s.run(7000);                 // grace over + 5 s delay
  TEST_ASSERT_FALSE(s.pc->relayOn());
  TEST_ASSERT_EQUAL(Fault::DRY_RUN, s.pc->fault());
  TEST_ASSERT_FALSE(s.pc->faultLatched());
}

void test_dry_run_retries_then_latches() {
  Sim s; s.level = 10; s.begin();
  for (int attempt = 1; attempt <= 3; attempt++) {
    // wait for (re)start: power-up delay or retry delay
    for (int i = 0; i < 400 && !s.pc->relayOn(); i++) s.step(200);
    TEST_ASSERT_TRUE_MESSAGE(s.pc->relayOn(), "pump should (re)start");
    s.watts = 50; s.amps = 0.5f;
    s.run(20000);
    TEST_ASSERT_EQUAL(Fault::DRY_RUN, s.pc->fault());
    if (attempt <= 2) {
      TEST_ASSERT_FALSE(s.pc->faultLatched());
      TEST_ASSERT_TRUE(s.pc->retryInS() > 0);
      s.run(61000);  // retry delay 1 min
      TEST_ASSERT_EQUAL(Fault::NONE, s.pc->fault());
    } else {
      TEST_ASSERT_TRUE(s.pc->faultLatched());
    }
  }
  s.run(5 * 60000);
  TEST_ASSERT_FALSE(s.pc->relayOn());
}

void test_no_rise_trips() {
  Sim s; s.cfg.noRiseEnabled = true; s.cfg.noRiseWindowMin = 1; s.cfg.noRiseMinPct = 2;
  s.level = 10; s.begin();
  s.runLoaded(6000);
  TEST_ASSERT_TRUE(s.pc->relayOn());
  s.level = 15; s.runLoaded(61000);   // rose 5 % in first window - ok
  TEST_ASSERT_TRUE(s.pc->relayOn());
  s.level = 15.5f; s.runLoaded(61000); // only 0.5 % in next window
  TEST_ASSERT_FALSE(s.pc->relayOn());
  TEST_ASSERT_EQUAL(Fault::NO_RISE, s.pc->fault());
}

void test_level_sensor_loss_stops_and_resumes() {
  Sim s; s.level = 10; s.begin();
  s.runLoaded(6000);
  TEST_ASSERT_TRUE(s.pc->relayOn());
  s.levelValid = false;
  s.runLoaded(5000);                     // short dropout tolerated
  TEST_ASSERT_TRUE(s.pc->relayOn());
  s.runLoaded(6000);                     // > 10 s timeout
  TEST_ASSERT_FALSE(s.pc->relayOn());
  TEST_ASSERT_EQUAL(Fault::LEVEL_SENSOR, s.pc->fault());
  s.levelValid = true; s.level = 40;     // above start point, but fill was in progress
  s.runLoaded(400);
  TEST_ASSERT_EQUAL(Fault::NONE, s.pc->fault());
  s.runLoaded(6000);
  TEST_ASSERT_TRUE(s.pc->relayOn());
}

void test_no_reading_at_boot_waits_then_faults() {
  Sim s; s.levelValid = false; s.begin();
  s.run(5000);
  TEST_ASSERT_EQUAL(Fault::NONE, s.pc->fault());
  TEST_ASSERT_EQUAL(Hold::WAIT_SENSOR, s.pc->hold());
  s.run(6000);
  TEST_ASSERT_EQUAL(Fault::LEVEL_SENSOR, s.pc->fault());
  TEST_ASSERT_FALSE(s.pc->relayOn());
}

void test_under_voltage_trips_and_restores_after_delay() {
  Sim s; s.cfg.voltageRestoreS = 30; s.level = 10; s.begin();
  s.runLoaded(6000);
  TEST_ASSERT_TRUE(s.pc->relayOn());
  s.volts = 170; s.runLoaded(5400);
  TEST_ASSERT_FALSE(s.pc->relayOn());
  TEST_ASSERT_EQUAL(Fault::UNDER_VOLTAGE, s.pc->fault());
  s.volts = 182; s.runLoaded(40000);    // inside hysteresis band (< 180+5): stays tripped
  TEST_ASSERT_EQUAL(Fault::UNDER_VOLTAGE, s.pc->fault());
  s.volts = 225; s.runLoaded(29000);
  TEST_ASSERT_EQUAL(Fault::UNDER_VOLTAGE, s.pc->fault());
  s.runLoaded(1400);
  TEST_ASSERT_EQUAL(Fault::NONE, s.pc->fault());
  s.runLoaded(6000);
  TEST_ASSERT_TRUE(s.pc->relayOn());
}

void test_over_voltage_blocks_start_when_idle() {
  Sim s; s.level = 10; s.volts = 275; s.begin();
  s.run(10000);
  TEST_ASSERT_FALSE(s.pc->relayOn());
  TEST_ASSERT_EQUAL(Fault::OVER_VOLTAGE, s.pc->fault());
}

void test_modes() {
  Sim s; s.level = 10; s.begin();
  s.pc->setMode(Mode::MANUAL);
  s.runLoaded(10000);
  TEST_ASSERT_FALSE(s.pc->relayOn());              // MANUAL never auto-starts
  TEST_ASSERT_TRUE(s.pc->requestStart());
  s.runLoaded(400);
  TEST_ASSERT_TRUE(s.pc->relayOn());
  s.level = 95; s.runLoaded(400);
  TEST_ASSERT_FALSE(s.pc->relayOn());              // still stops at full
  TEST_ASSERT_FALSE(s.pc->requestStart());         // refused while full
  s.level = 50; s.step();
  TEST_ASSERT_TRUE(s.pc->requestStart());
  s.runLoaded(6000);
  TEST_ASSERT_TRUE(s.pc->relayOn());
  s.pc->setMode(Mode::OFF);
  TEST_ASSERT_FALSE(s.pc->relayOn());
  TEST_ASSERT_FALSE(s.pc->requestStart());
  s.level = 5; s.runLoaded(10000);
  TEST_ASSERT_FALSE(s.pc->relayOn());
  TEST_ASSERT_EQUAL(Hold::MODE_OFF, s.pc->hold());
}

void test_manual_stop_in_auto() {
  Sim s; s.level = 10; s.begin();
  s.runLoaded(6000);
  TEST_ASSERT_TRUE(s.pc->relayOn());
  s.level = 40; s.runLoaded(400);
  s.pc->requestStop();
  TEST_ASSERT_FALSE(s.pc->relayOn());
  s.runLoaded(20000);
  TEST_ASSERT_FALSE(s.pc->relayOn());              // waits for next low level
}

void test_sump_empty_holds_pump() {
  Sim s; s.cfg.sumpFloatEnabled = true; s.level = 10; s.begin();
  s.runLoaded(6000);
  TEST_ASSERT_TRUE(s.pc->relayOn());
  s.sump = false; s.runLoaded(2000);
  TEST_ASSERT_TRUE(s.pc->relayOn());               // debounce
  s.runLoaded(1400);
  TEST_ASSERT_FALSE(s.pc->relayOn());
  TEST_ASSERT_EQUAL(Hold::SUMP_EMPTY, s.pc->hold());
  s.sump = true; s.runLoaded(6000);
  TEST_ASSERT_TRUE(s.pc->relayOn());
}

void test_power_meter_loss_while_running() {
  Sim s; s.level = 10; s.begin();
  s.runLoaded(6000);
  TEST_ASSERT_TRUE(s.pc->relayOn());
  s.powerValid = false; s.run(11000);
  TEST_ASSERT_FALSE(s.pc->relayOn());
  TEST_ASSERT_EQUAL(Fault::POWER_METER, s.pc->fault());
  s.powerValid = true; s.runLoaded(400);
  TEST_ASSERT_EQUAL(Fault::NONE, s.pc->fault());
}

void test_max_runtime() {
  Sim s; s.cfg.maxRunMin = 2; s.level = 10; s.begin();
  s.runLoaded(6000);
  TEST_ASSERT_TRUE(s.pc->relayOn());
  s.runLoaded(121000);
  TEST_ASSERT_EQUAL(Fault::MAX_RUNTIME, s.pc->fault());
  TEST_ASSERT_FALSE(s.pc->relayOn());
}

void test_millis_rollover() {
  Sim s(0xFFFFFFFFu - 3000); s.level = 10; s.begin();
  s.runLoaded(6000);                               // wraps past 0
  TEST_ASSERT_TRUE(s.pc->relayOn());
  s.runLoaded(30000);
  TEST_ASSERT_TRUE(s.pc->relayOn());
  TEST_ASSERT_EQUAL(Fault::NONE, s.pc->fault());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_powerup_delay_then_auto_start_and_stop_at_full);
  RUN_TEST(test_hysteresis_no_start_between_band);
  RUN_TEST(test_overload_latches_and_needs_reset);
  RUN_TEST(test_short_inrush_does_not_trip_overload);
  RUN_TEST(test_dry_run_ignored_during_grace_then_trips);
  RUN_TEST(test_dry_run_retries_then_latches);
  RUN_TEST(test_no_rise_trips);
  RUN_TEST(test_level_sensor_loss_stops_and_resumes);
  RUN_TEST(test_no_reading_at_boot_waits_then_faults);
  RUN_TEST(test_under_voltage_trips_and_restores_after_delay);
  RUN_TEST(test_over_voltage_blocks_start_when_idle);
  RUN_TEST(test_modes);
  RUN_TEST(test_manual_stop_in_auto);
  RUN_TEST(test_sump_empty_holds_pump);
  RUN_TEST(test_power_meter_loss_while_running);
  RUN_TEST(test_max_runtime);
  RUN_TEST(test_millis_rollover);
  return UNITY_END();
}
