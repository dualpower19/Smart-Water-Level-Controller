#pragma once
// Wi-Fi station with automatic setup access point fallback, mDNS and NTP.
//  * No saved network, or not connected for 60 s -> AP "WaterLevel-Setup" (192.168.4.1) comes up
//  * Connected for 60 s -> AP shuts down again
// The pump control never depends on Wi-Fi.
#include <Arduino.h>

namespace WifiMgr {
  void begin();
  void loop();
  bool connected();
  bool apActive();
  String ip();
  bool timeValid();
}
