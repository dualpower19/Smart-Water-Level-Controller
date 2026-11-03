#pragma once
// Shared application state. loop() and the async web server run in different
// FreeRTOS tasks, so everything here is accessed under App::lock().
#include <Arduino.h>
#include "Settings.h"
#include "PumpController.h"
#include "LevelSensor.h"
#include "CurrentSensor.h"

struct LogEntry {
  uint32_t epoch;   // 0 if time not synced yet
  uint32_t uptimeS;
  char text[56];
};

class EventLog {
 public:
  static constexpr int N = 40;
  void add(const char* fmt, ...);
  int count() const { return count_; }
  const LogEntry& at(int i) const { return e_[(head_ - 1 - i + N) % N]; }  // 0 = newest
 private:
  LogEntry e_[N];
  int head_ = 0, count_ = 0;
};

enum class Cmd : uint8_t { NONE, START, STOP, RESET, MODE_AUTO, MODE_MANUAL, MODE_OFF };

struct App {
  Settings settings;
  wlc::PumpController* pump = nullptr;
  LevelSensor level;
  CurrentSensor elec;
  EventLog log;

  LevelReading lastLevel;
  ElecReading lastElec;
  bool sumpHasWater = true;

  // deferred work requested by the web server, executed in loop()
  volatile bool saveRequested = false;
  volatile bool restartRequested = false;
  volatile uint32_t restartAtMs = 0;

  SemaphoreHandle_t mutex = nullptr;
  void lock()   { xSemaphoreTakeRecursive(mutex, portMAX_DELAY); }
  void unlock() { xSemaphoreGiveRecursive(mutex); }

  // Applies a command immediately (caller must hold the lock). Returns false if refused.
  bool command(Cmd c);
  void applySettings();  // push settings into controller + sensors (caller holds lock)
};

struct AppLock {
  explicit AppLock(App& a) : a_(a) { a_.lock(); }
  ~AppLock() { a_.unlock(); }
  App& a_;
};

extern App app;
