#pragma once
// HTTP dashboard + JSON API on port 80.
//
//  GET  /                 dashboard
//  GET  /api/status       live state
//  GET  /api/events       recent event log
//  GET  /api/config       settings (passwords omitted)
//  POST /api/config       JSON body, partial update            [auth]
//  POST /api/cmd?cmd=X    start|stop|reset|auto|manual|off|
//                         cal_empty|cal_full|energy_reset|restart|factory_reset  [auth]
//  POST /api/update       multipart firmware.bin (OTA)         [auth]
//
// [auth] = HTTP Basic, user "admin", password = admin password from settings.
#include <Arduino.h>

namespace WebUI {
  void begin();
}
