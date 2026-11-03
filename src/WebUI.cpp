#include "WebUI.h"
#include <WiFi.h>
#include <Update.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include "config.h"
#include "App.h"
#include "WifiManager.h"
#include "web_index.h"

using namespace wlc;

static AsyncWebServer server(80);

static bool authed(AsyncWebServerRequest* r) {
  String pass;
  { AppLock l(app); pass = app.settings.net.adminPass; }
  if (r->authenticate("admin", pass.c_str())) return true;
  // No WWW-Authenticate header on purpose: the dashboard asks for the password itself.
  r->send(401, "application/json", "{\"error\":\"unauthorized\"}");
  return false;
}

static void sendJson(AsyncWebServerRequest* r, JsonDocument& doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  r->send(code, "application/json", out);
}

static void fillStatus(JsonObject o) {
  const LevelReading& lr = app.lastLevel;
  const ElecReading& er = app.lastElec;
  PumpController& p = *app.pump;
  const uint32_t now = millis();

  o["fw"] = FW_VERSION;
  o["uptime"] = now / 1000;
  o["time"] = WifiMgr::timeValid() ? (uint32_t)time(nullptr) : 0;
  o["ip"] = WifiMgr::ip();
  o["rssi"] = WifiMgr::connected() ? WiFi.RSSI() : 0;
  o["ap"] = WifiMgr::apActive();
  o["host"] = app.settings.net.hostname;

  JsonObject l = o["level"].to<JsonObject>();
  l["ok"] = p.levelOk();
  if (!isnan(lr.levelPct)) {
    l["pct"] = serialized(String(lr.levelPct, 1));
    l["litres"] = serialized(String(lr.litres, 0));
    l["distCm"] = serialized(String(lr.distanceCm, 1));
  }
  l["raw"] = lr.raw;
  l["ageMs"] = lr.seq ? now - lr.atMs : 0;
  l["ok_n"] = lr.okCount;
  l["err_n"] = lr.errCount;
  l["lastErr"] = lr.lastError;
  l["start"] = app.settings.ctl.startLevelPct;
  l["stop"] = app.settings.ctl.stopLevelPct;
  l["capacity"] = app.settings.tank.capacityL;

  JsonObject w = o["elec"].to<JsonObject>();
  w["ok"] = p.currentOk();
  if (!isnan(er.current)) {
    w["a"] = serialized(String(er.current, 2));
    w["rawA"] = serialized(String(er.currentRawA, 3));
    w["ctRmsV"] = serialized(String(er.ctRmsV, 4));
    w["ctBiasV"] = serialized(String(er.ctBiasV, 3));
  }
  w["samples"] = er.samples;
  w["err_n"] = er.errCount;
  w["vEnabled"] = app.settings.ctl.voltageEnabled;
  w["vOk"] = p.voltageOk();
  if (!isnan(er.voltage)) {
    w["v"] = serialized(String(er.voltage, 1));
    w["vtBiasV"] = serialized(String(er.vtBiasV, 3));
  }
  w["overA"] = app.settings.ctl.overloadCurrentA;
  w["underA"] = app.settings.ctl.underloadCurrentA;

  JsonObject u = o["pump"].to<JsonObject>();
  u["on"] = p.relayOn();
  u["mode"] = PumpController::modeName(p.mode());
  u["fault"] = (uint8_t)p.fault();
  u["faultName"] = PumpController::faultName(p.fault());
  u["latched"] = p.faultLatched();
  u["hold"] = PumpController::holdName(p.hold());
  u["fill"] = p.fillRequested();
  u["retryIn"] = p.retryInS();
  u["minOff"] = p.minOffRemainingS();
  u["runS"] = p.runTimeS();
  u["retries"] = p.retries();
  u["starts"] = p.startCount();
  u["totalRunS"] = p.totalRunS();

  JsonObject s = o["sump"].to<JsonObject>();
  s["enabled"] = app.settings.ctl.sumpFloatEnabled;
  s["water"] = app.sumpHasWater;
}

static void handleCmd(AsyncWebServerRequest* r) {
  if (!authed(r)) return;
  String cmd;
  if (r->hasParam("cmd", true)) cmd = r->getParam("cmd", true)->value();
  else if (r->hasParam("cmd")) cmd = r->getParam("cmd")->value();
  float value = NAN;
  if (r->hasParam("value", true)) value = r->getParam("value", true)->value().toFloat();
  else if (r->hasParam("value")) value = r->getParam("value")->value().toFloat();

  bool ok = true;
  String msg = "ok";
  {
    AppLock l(app);
    if (cmd == "start")       { ok = app.command(Cmd::START); if (!ok) msg = "refused (mode OFF, locked fault or tank full)"; }
    else if (cmd == "stop")   app.command(Cmd::STOP);
    else if (cmd == "reset")  app.command(Cmd::RESET);
    else if (cmd == "auto")   app.command(Cmd::MODE_AUTO);
    else if (cmd == "manual") app.command(Cmd::MODE_MANUAL);
    else if (cmd == "off")    app.command(Cmd::MODE_OFF);
    else if (cmd == "cal_empty" || cmd == "cal_full") {
      const float d = app.lastLevel.distanceCm;
      if (isnan(d) || !app.pump->levelOk()) { ok = false; msg = "no valid sensor reading"; }
      else {
        if (cmd == "cal_empty") app.settings.tank.emptyDistCm = d;
        else app.settings.tank.fullDistCm = d;
        SettingsStore::validate(app.settings);
        app.applySettings();
        app.saveRequested = true;
        msg = String(cmd == "cal_empty" ? "empty" : "full") + " = " + String(d, 1) + " cm";
      }
    }
    else if (cmd == "cal_ct_zero") {
      // Pump must be OFF: whatever the CT reads now is noise / offset.
      const ElecReading& e = app.lastElec;
      if (app.pump->relayOn()) { ok = false; msg = "stop the pump first"; }
      else if (!e.currentValid || isnan(e.currentRawA)) { ok = false; msg = "CT input invalid (check bias network)"; }
      else {
        app.settings.ct.zeroA = e.currentRawA;
        SettingsStore::validate(app.settings);
        app.applySettings(); app.saveRequested = true;
        msg = "zero = " + String(app.settings.ct.zeroA, 3) + " A";
      }
    }
    else if (cmd == "cal_ct") {
      // Pump running; value = current measured with a clamp meter on the same wire.
      const ElecReading& e = app.lastElec;
      if (!(value > 0.2f)) { ok = false; msg = "enter the clamp-meter reading in A"; }
      else if (!e.currentValid || !(e.ctRmsV > 0.005f)) { ok = false; msg = "signal too small - is the pump running?"; }
      else {
        const float z = app.settings.ct.zeroA;
        app.settings.ct.ampsPerVolt = sqrtf(value * value + z * z) / e.ctRmsV;
        SettingsStore::validate(app.settings);
        app.applySettings(); app.saveRequested = true;
        msg = "CT factor = " + String(app.settings.ct.ampsPerVolt, 2) + " A/V";
      }
    }
    else if (cmd == "cal_volt") {
      const ElecReading& e = app.lastElec;
      if (!(value > 50.0f)) { ok = false; msg = "enter the mains voltage from a meter"; }
      else if (!e.voltageValid || !(e.vtRmsV > 0.01f)) { ok = false; msg = "no voltage signal (enable + wire ZMPT101B)"; }
      else {
        app.settings.ct.voltsPerVolt = value / e.vtRmsV;
        SettingsStore::validate(app.settings);
        app.applySettings(); app.saveRequested = true;
        msg = "voltage factor = " + String(app.settings.ct.voltsPerVolt, 1) + " V/V";
      }
    }
    else if (cmd == "restart") { app.restartRequested = true; app.restartAtMs = millis() + 500; }
    else if (cmd == "factory_reset") {
      SettingsStore::factoryReset(app.settings);
      app.restartRequested = true; app.restartAtMs = millis() + 500;
    }
    else { ok = false; msg = "unknown cmd"; }
    if (ok) app.log.add("Web: %s", cmd.c_str());
  }
  JsonDocument d;
  d["ok"] = ok;
  d["msg"] = msg;
  sendJson(r, d, ok ? 200 : 400);
}

void WebUI::begin() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send(200, "text/html", INDEX_HTML);
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument d;
    { AppLock l(app); fillStatus(d.to<JsonObject>()); }
    sendJson(r, d);
  });

  server.on("/api/events", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument d;
    JsonArray a = d.to<JsonArray>();
    {
      AppLock l(app);
      for (int i = 0; i < app.log.count(); i++) {
        const LogEntry& e = app.log.at(i);
        JsonObject o = a.add<JsonObject>();
        o["t"] = e.epoch;
        o["up"] = e.uptimeS;
        o["msg"] = e.text;
      }
    }
    sendJson(r, d);
  });

  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument d;
    { AppLock l(app); SettingsStore::toJson(app.settings, d.to<JsonObject>()); }
    sendJson(r, d);
  });

  auto* cfgHandler = new AsyncCallbackJsonWebHandler("/api/config",
    [](AsyncWebServerRequest* r, JsonVariant& json) {
      if (!authed(r)) return;
      bool restart = false;
      {
        AppLock l(app);
        const NetConfig before = app.settings.net;
        SettingsStore::fromJson(app.settings, json.as<JsonObjectConst>());
        app.applySettings();
        app.saveRequested = true;
        restart = strcmp(before.ssid, app.settings.net.ssid) != 0 ||
                  strcmp(before.pass, app.settings.net.pass) != 0 ||
                  strcmp(before.hostname, app.settings.net.hostname) != 0 ||
                  strcmp(before.tz, app.settings.net.tz) != 0;
        if (restart) { app.restartRequested = true; app.restartAtMs = millis() + 1500; }
        app.log.add("Web: settings saved%s", restart ? ", restarting" : "");
      }
      JsonDocument d;
      d["ok"] = true;
      d["restart"] = restart;
      sendJson(r, d);
    });
  cfgHandler->setMethod(HTTP_POST);
  cfgHandler->setMaxContentLength(4096);
  server.addHandler(cfgHandler);

  server.on("/api/cmd", HTTP_POST, handleCmd);

  // OTA firmware update (upload firmware.bin from .pio/build/esp32dev/)
  server.on("/api/update", HTTP_POST,
    [](AsyncWebServerRequest* r) {
      if (!authed(r)) return;
      const bool ok = !Update.hasError();
      r->send(ok ? 200 : 500, "application/json",
              ok ? "{\"ok\":true,\"msg\":\"updated, restarting\"}" : "{\"ok\":false,\"msg\":\"update failed\"}");
      if (ok) { AppLock l(app); app.restartRequested = true; app.restartAtMs = millis() + 1000; }
    },
    [](AsyncWebServerRequest* r, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
      static bool allowed = false;
      if (index == 0) {
        String pass;
        { AppLock l(app); pass = app.settings.net.adminPass; }
        allowed = r->authenticate("admin", pass.c_str());
        if (!allowed) return;
        { AppLock l(app); app.log.add("OTA start %s", filename.c_str()); }
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
      }
      if (!allowed) return;
      if (len && Update.write(data, len) != len) Update.printError(Serial);
      if (final) {
        if (Update.end(true)) { AppLock l(app); app.log.add("OTA ok, %u bytes", (unsigned)(index + len)); }
        else Update.printError(Serial);
      }
    });

  server.onNotFound([](AsyncWebServerRequest* r) {
    // captive-portal style: send unknown paths to the dashboard while in AP mode
    if (WifiMgr::apActive() && r->method() == HTTP_GET) r->redirect("/");
    else r->send(404, "text/plain", "not found");
  });

  DefaultHeaders::Instance().addHeader("Cache-Control", "no-store");
  server.begin();
}
