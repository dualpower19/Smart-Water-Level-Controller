// Smart Water Level Controller - ESP32
// RS485 ultrasonic level sensor + PZEM-004T pump monitor + contactor relay,
// with OLED display and Wi-Fi dashboard. See README.md for wiring.
#include <Arduino.h>
#include <stdarg.h>
#include <time.h>
#include <driver/gpio.h>
#include "config.h"
#include "App.h"
#include "Display.h"
#include "WifiManager.h"
#include "WebUI.h"

using namespace wlc;

App app;

// ---------------------------------------------------------------------------
// EventLog / App
// ---------------------------------------------------------------------------
void EventLog::add(const char* fmt, ...) {
  LogEntry& e = e_[head_];
  const time_t t = time(nullptr);
  e.epoch = t > 1700000000 ? (uint32_t)t : 0;
  e.uptimeS = millis() / 1000;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(e.text, sizeof(e.text), fmt, ap);
  va_end(ap);
  Serial.printf("[%7lus] %s\n", (unsigned long)e.uptimeS, e.text);
  head_ = (head_ + 1) % N;
  if (count_ < N) count_++;
}

bool App::command(Cmd c) {
  switch (c) {
    case Cmd::START:       return pump->requestStart();
    case Cmd::STOP:        pump->requestStop(); return true;
    case Cmd::RESET:       pump->resetFault(); return true;
    case Cmd::MODE_AUTO:   pump->setMode(Mode::AUTO); return true;
    case Cmd::MODE_MANUAL: pump->setMode(Mode::MANUAL); return true;
    case Cmd::MODE_OFF:    pump->setMode(Mode::OFF); return true;
    default:               return false;
  }
}

void App::applySettings() {
  pump->setConfig(settings.ctl);
  level.setConfig(settings.sensor, settings.tank);
}

// ---------------------------------------------------------------------------
// I/O helpers
// ---------------------------------------------------------------------------
static inline void relayWrite(bool on) {
  digitalWrite(PIN_PUMP_RELAY, (on == (bool)RELAY_ACTIVE_HIGH) ? HIGH : LOW);
}

static uint32_t faultSinceMs = 0;
static bool buzzerSilenced = false;

static void onPumpEvent(Event e, Fault f) {
  const PowerReading& p = app.lastPower;
  switch (e) {
    case Event::PUMP_START:
      app.log.add("Pump ON  (level %.0f%%)", app.pump->levelPct());
      break;
    case Event::PUMP_STOP:
      app.log.add("Pump OFF (level %.0f%%)", app.pump->levelPct());
      break;
    case Event::FAULT_TRIP:
      app.log.add("FAULT %s%s %.0fV %.1fA %.0fW", PumpController::faultName(f),
                  app.pump->faultLatched() ? " [LOCK]" : "", p.voltage, p.current, p.power);
      faultSinceMs = millis();
      buzzerSilenced = false;
      break;
    case Event::FAULT_CLEAR:
      app.log.add("Cleared: %s", PumpController::faultName(f));
      break;
    case Event::MODE_CHANGE:
      app.log.add("Mode -> %s", PumpController::modeName(app.pump->mode()));
      break;
  }
}

// Push button: short = reset locked fault / silence buzzer / start-stop, long (3 s) = cycle mode
static void handleButton(uint32_t now) {
  static bool down = false, longFired = false;
  static uint32_t downAt = 0, lastChange = 0;
  const bool pressed = digitalRead(PIN_BUTTON) == LOW;
  if (pressed != down && now - lastChange > 40) {
    lastChange = now;
    down = pressed;
    if (pressed) { downAt = now; longFired = false; return; }
    if (longFired) return;
    // short press released
    if (app.pump->fault() != Fault::NONE) {
      if (app.pump->faultLatched()) { app.pump->resetFault(); app.log.add("Button: fault reset"); }
      else buzzerSilenced = true;
    } else if (app.pump->relayOn() || app.pump->fillRequested()) {
      app.pump->requestStop();
      app.log.add("Button: stop");
    } else if (app.pump->requestStart()) {
      app.log.add("Button: start");
    }
  }
  if (down && !longFired && now - downAt >= 3000) {
    longFired = true;
    const Mode next = app.pump->mode() == Mode::AUTO ? Mode::MANUAL
                    : app.pump->mode() == Mode::MANUAL ? Mode::OFF : Mode::AUTO;
    app.pump->setMode(next);
  }
}

static void updateIndicators(uint32_t now) {
  const bool fault = app.pump->fault() != Fault::NONE;
  const bool on = app.pump->relayOn();

  bool led;
  if (fault) led = (now / 125) % 2;
  else if (on) led = true;
  else led = (now % 2000) < 80;
  digitalWrite(PIN_STATUS_LED, led ? HIGH : LOW);

  if (PIN_BUZZER >= 0) {
    bool beep = false;
    if (fault && !buzzerSilenced) {
      if (app.pump->faultLatched()) beep = (now % 5000) < 150;               // until reset
      else if (now - faultSinceMs < 10000) beep = (now % 1000) < 100;       // first 10 s
    }
    digitalWrite(PIN_BUZZER, beep ? HIGH : LOW);
  }
}

// ---------------------------------------------------------------------------
void setup() {
  // Preload the OFF level into the GPIO output register before the pin becomes an
  // output, so an active-low relay board never blips at boot.
  gpio_set_level((gpio_num_t)PIN_PUMP_RELAY, RELAY_ACTIVE_HIGH ? 0 : 1);
  pinMode(PIN_PUMP_RELAY, OUTPUT);
  relayWrite(false);

  pinMode(PIN_STATUS_LED, OUTPUT);
  if (PIN_BUZZER >= 0) { pinMode(PIN_BUZZER, OUTPUT); digitalWrite(PIN_BUZZER, LOW); }
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_SUMP_FLOAT, INPUT_PULLUP);

  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n" FW_NAME " v" FW_VERSION));

  app.mutex = xSemaphoreCreateRecursiveMutex();
  AppLock l(app);

  SettingsStore::load(app.settings);
  app.pump = new PumpController(app.settings.ctl);
  app.pump->onEvent(onPumpEvent);
  app.pump->setMode((Mode)app.settings.mode);
  app.pump->begin(millis());
  app.log.add("Boot v" FW_VERSION ", mode %s", PumpController::modeName(app.pump->mode()));

  if (!Display::begin()) app.log.add("OLED not found");
  app.level.begin(app.settings.sensor, app.settings.tank);
  app.power.begin();
  WifiMgr::begin();
  WebUI::begin();

  enableLoopWDT();  // reboot (relay drops out) if loop() ever hangs for > 5 s
}

void loop() {
  static uint32_t lastTick = 0, lastDisplay = 0;
  static uint32_t levelSeq = 0, powerSeq = 0;
  const uint32_t now = millis();

  {
    AppLock l(app);
    app.power.poll();

    if (now - lastTick >= CONTROL_TICK_MS) {
      lastTick = now;
      const LevelReading lr = app.level.read();
      const PowerReading pr = app.power.read();
      app.lastLevel = lr;
      app.lastPower = pr;
      app.sumpHasWater = digitalRead(PIN_SUMP_FLOAT) == LOW;

      Inputs in;
      in.nowMs = now;
      in.levelValid = lr.seq != levelSeq;
      in.levelPct = lr.levelPct;
      in.powerValid = pr.seq != powerSeq;
      in.voltage = pr.voltage; in.current = pr.current; in.power = pr.power;
      in.sumpHasWater = app.sumpHasWater;
      levelSeq = lr.seq;
      powerSeq = pr.seq;
      app.pump->update(in);

      if ((uint8_t)app.pump->mode() != app.settings.mode) {
        app.settings.mode = (uint8_t)app.pump->mode();
        app.saveRequested = true;
      }
    }

    handleButton(now);
    relayWrite(app.pump->relayOn());
    updateIndicators(now);
    WifiMgr::loop();

    if (app.saveRequested) {
      app.saveRequested = false;
      SettingsStore::save(app.settings);
    }
    if (app.restartRequested && (int32_t)(now - app.restartAtMs) >= 0) {
      relayWrite(false);
      SettingsStore::save(app.settings);
      delay(100);
      ESP.restart();
    }
  }

  if (now - lastDisplay >= DISPLAY_MS) {
    lastDisplay = now;
    Display::render();
  }
  delay(2);
}
