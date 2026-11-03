#include "PowerMonitor.h"
#include <PZEM004Tv30.h>
#include "config.h"

static PZEM004Tv30* pzem = nullptr;

void PowerMonitor::begin() {
  pzem = new PZEM004Tv30(Serial1, PIN_PZEM_RX, PIN_PZEM_TX);
}

void PowerMonitor::poll() {
  if (!pzem) return;
  const uint32_t now = millis();
  if (now - lastPoll_ < POWER_POLL_MS) return;
  lastPoll_ = now;

  // The library reads all registers in one transaction and caches them,
  // so the calls after voltage() don't hit the bus again.
  const float v = pzem->voltage();
  if (isnan(v)) { r_.errCount++; return; }
  const float i = pzem->current();
  const float p = pzem->power();
  if (isnan(i) || isnan(p)) { r_.errCount++; return; }

  r_.voltage = v;
  r_.current = i;
  r_.power = p;
  r_.energyKWh = pzem->energy();
  r_.frequency = pzem->frequency();
  r_.pf = pzem->pf();
  r_.atMs = now;
  r_.seq++;
  r_.okCount++;
}

bool PowerMonitor::resetEnergy() { return pzem && pzem->resetEnergy(); }
