#include "WifiManager.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <time.h>
#include "config.h"
#include "App.h"

namespace WifiMgr {

static bool ap = false;
static uint32_t stateSince = 0;
static bool wasConnected = false;
static bool mdnsStarted = false;

static void startAp() {
  if (ap) return;
  WiFi.mode(app.settings.net.ssid[0] ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  ap = true;
  app.log.add("Setup AP on, %s", WiFi.softAPIP().toString().c_str());
}

static void stopAp() {
  if (!ap) return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  ap = false;
  app.log.add("Setup AP off");
}

void begin() {
  WiFi.persistent(false);
  WiFi.setHostname(app.settings.net.hostname);
  configTzTime(app.settings.net.tz, "pool.ntp.org", "time.google.com");

  if (app.settings.net.ssid[0]) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(app.settings.net.ssid, app.settings.net.pass);
  } else {
    startAp();
  }
  stateSince = millis();
}

void loop() {
  const bool c = WiFi.status() == WL_CONNECTED;
  const uint32_t now = millis();
  if (c != wasConnected) {
    wasConnected = c;
    stateSince = now;
    if (c) {
      app.log.add("WiFi connected %s", WiFi.localIP().toString().c_str());
      if (!mdnsStarted && MDNS.begin(app.settings.net.hostname)) {
        MDNS.addService("http", "tcp", 80);
        mdnsStarted = true;
      }
    } else {
      app.log.add("WiFi disconnected");
    }
  }
  if (!c && !ap && now - stateSince > 60000) startAp();
  if (c && ap && now - stateSince > 60000) stopAp();
}

bool connected() { return WiFi.status() == WL_CONNECTED; }
bool apActive() { return ap; }
String ip() { return connected() ? WiFi.localIP().toString() : (ap ? WiFi.softAPIP().toString() : String("-")); }
bool timeValid() { return time(nullptr) > 1700000000; }

}  // namespace WifiMgr
