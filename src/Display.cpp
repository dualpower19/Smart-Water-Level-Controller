#include "Display.h"
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "App.h"
#include "WifiManager.h"

using namespace wlc;

static Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
static bool present = false;

namespace Display {

bool begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  present = oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);
  if (!present) return false;
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println(F("Smart Water Level"));
  oled.println(F("Controller"));
  oled.println();
  oled.println(F("v" FW_VERSION));
  oled.println(F("Starting..."));
  oled.display();
  return true;
}

static void drawTank(float pct, bool valid, bool pumpOn, uint32_t t) {
  const int x = 0, y = 0, w = 28, h = 64;
  oled.drawRoundRect(x, y, w, h, 3, SSD1306_WHITE);
  if (valid) {
    const int fill = (int)((h - 4) * pct / 100.0f + 0.5f);
    oled.fillRect(x + 2, y + h - 2 - fill, w - 4, fill, SSD1306_WHITE);
  } else {
    oled.drawLine(x + 3, y + 3, x + w - 4, y + h - 4, SSD1306_WHITE);
    oled.drawLine(x + w - 4, y + 3, x + 3, y + h - 4, SSD1306_WHITE);
  }
  // animated inflow arrow while pumping
  if (pumpOn && valid) {
    const int ay = 4 + (t / 250) % 4;
    oled.fillTriangle(x + w / 2 - 4, ay, x + w / 2 + 4, ay, x + w / 2, ay + 5, SSD1306_INVERSE);
  }
}

void render() {
  if (!present) return;

  // Snapshot state under the lock, then draw without it.
  float pct, litres, volts, amps, watts;
  bool levelValid, powerValid, pumpOn, latched;
  Fault fault; Hold hold; Mode mode;
  uint32_t retryIn, minOff, runS;
  {
    AppLock l(app);
    const LevelReading& lr = app.lastLevel;
    const PowerReading& pr = app.lastPower;
    levelValid = app.pump->levelOk();
    pct = lr.levelPct; litres = lr.litres;
    powerValid = app.pump->powerOk();
    volts = pr.voltage; amps = pr.current; watts = pr.power;
    pumpOn = app.pump->relayOn();
    fault = app.pump->fault(); latched = app.pump->faultLatched();
    hold = app.pump->hold(); mode = app.pump->mode();
    retryIn = app.pump->retryInS(); minOff = app.pump->minOffRemainingS();
    runS = app.pump->runTimeS();
  }

  const uint32_t t = millis();
  oled.clearDisplay();
  drawTank(pct, levelValid, pumpOn, t);

  const int X = 34;
  char buf[24];

  oled.setTextSize(2);
  oled.setCursor(X, 0);
  if (levelValid) snprintf(buf, sizeof(buf), "%3d%%", (int)(pct + 0.5f));
  else snprintf(buf, sizeof(buf), " --%%");
  oled.print(buf);

  oled.setTextSize(1);
  oled.setCursor(X, 17);
  if (levelValid) snprintf(buf, sizeof(buf), "%.0f L", litres);
  else snprintf(buf, sizeof(buf), "no sensor");
  oled.print(buf);

  oled.setCursor(X, 28);
  if (pumpOn) snprintf(buf, sizeof(buf), "PUMP ON %02lu:%02lu", (unsigned long)(runS / 60), (unsigned long)(runS % 60));
  else snprintf(buf, sizeof(buf), "PUMP OFF");
  oled.print(buf);

  oled.setCursor(X, 38);
  if (powerValid) snprintf(buf, sizeof(buf), "%.0fV %.1fA", volts, amps);
  else snprintf(buf, sizeof(buf), "meter offline");
  oled.print(buf);
  if (powerValid && pumpOn) {
    oled.setCursor(X, 47);
    snprintf(buf, sizeof(buf), "%.0fW", watts);
    oled.print(buf);
  }

  // bottom line: fault (blinking) or status; alternates with IP every 3 s
  oled.setCursor(X, 56);
  if (fault != Fault::NONE) {
    if ((t / 500) % 2 == 0) oled.fillRect(X - 1, 55, OLED_WIDTH - X + 1, 9, SSD1306_WHITE);
    oled.setTextColor(SSD1306_INVERSE);
    if (retryIn > 0 && (t / 3000) % 2) snprintf(buf, sizeof(buf), "retry %lum%02lus", (unsigned long)(retryIn / 60), (unsigned long)(retryIn % 60));
    else snprintf(buf, sizeof(buf), "%s%.14s", latched ? "!" : "", PumpController::faultName(fault));
    oled.print(buf);
    oled.setTextColor(SSD1306_WHITE);
  } else if ((t / 3000) % 2 && (WifiMgr::connected() || WifiMgr::apActive())) {
    oled.print(WifiMgr::apActive() && !WifiMgr::connected() ? "AP " + WifiMgr::ip() : WifiMgr::ip());
  } else {
    if (hold == Hold::MIN_OFF_TIME) snprintf(buf, sizeof(buf), "%s wait %lus", PumpController::modeName(mode), (unsigned long)minOff);
    else snprintf(buf, sizeof(buf), "%s %.9s", PumpController::modeName(mode), PumpController::holdName(hold));
    oled.print(buf);
  }
  oled.display();
}

}  // namespace Display
