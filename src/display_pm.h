// src/display_pm.h
#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "time.h"

#define DPM_BACKLIGHT_PIN 21

namespace display_pm {

// 8-bit ledc duty values for each brightness level.
// LOW=64 (~25%), MID=160 (~62%), HIGH=255 (100%)
static constexpr uint8_t LEVELS[3] = {64, 160, 255};
static constexpr uint32_t LEDC_FREQ_HZ = 5000;
static constexpr uint8_t  LEDC_RES_BITS = 8;
static constexpr uint8_t  LEDC_CHANNEL = 0;   // used only on Arduino-ESP32 2.x

static uint8_t  briLevel    = 2;   // 0=LOW 1=MID 2=HIGH
static uint8_t  qhStart     = 23;
static uint8_t  qhEnd       = 7;
static uint8_t  qhMode      = 0;   // 0=OFF 1=DIM 2=SLEEP
static uint8_t  cycMode     = 1;   // 0=OFF 1=60s 2=30s 3=120s
static bool     inQuiet     = false;
static unsigned long wakeUntilMs = 0;
static uint8_t  currentDuty = 0;
static uint8_t  targetDuty  = 0;
static unsigned long lastFadeMs = 0;
static bool     ledcOk = false;

inline void writeDuty(uint8_t duty) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(DPM_BACKLIGHT_PIN, duty);
#else
  ledcWrite(LEDC_CHANNEL, duty);
#endif
}

inline void init(Preferences& prefs) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcOk = ledcAttach(DPM_BACKLIGHT_PIN, LEDC_FREQ_HZ, LEDC_RES_BITS);
#else
  ledcSetup(LEDC_CHANNEL, LEDC_FREQ_HZ, LEDC_RES_BITS);
  ledcAttachPin(DPM_BACKLIGHT_PIN, LEDC_CHANNEL);
  ledcOk = true;
#endif
  if (!ledcOk) {
    pinMode(DPM_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(DPM_BACKLIGHT_PIN, HIGH);
  }

  prefs.begin("ohmyclawd", false);
  briLevel = prefs.getUChar("bri",  2);
  qhStart  = prefs.getUChar("qh_s", 23);
  qhEnd    = prefs.getUChar("qh_e", 7);
  qhMode   = prefs.getUChar("qh_m", 0);
  cycMode  = prefs.getUChar("cyc",  1);
  prefs.end();

  if (briLevel > 2) briLevel = 2;
  if (qhStart  > 23) qhStart = 23;
  if (qhEnd    > 23) qhEnd   = 7;
  if (qhMode   > 2)  qhMode  = 0;
  if (cycMode  > 3)  cycMode = 1;

  targetDuty  = LEVELS[briLevel];
  currentDuty = targetDuty;
  if (ledcOk) writeDuty(currentDuty);
}

inline void tick() {
  // 1) Determine whether we're in the quiet-hours window.
  struct tm ti;
  bool haveTime = getLocalTime(&ti, 5); // 5ms timeout, non-blocking
  if (haveTime && qhStart != qhEnd) {
    uint8_t h = ti.tm_hour;
    if (qhStart < qhEnd) {
      inQuiet = (h >= qhStart && h < qhEnd);
    } else {
      inQuiet = (h >= qhStart || h < qhEnd);
    }
  } else {
    inQuiet = false;
  }

  // 2) Choose target duty.
  if (!inQuiet || qhMode == 0) {
    targetDuty = LEVELS[briLevel];
  } else if (qhMode == 1) {
    targetDuty = LEVELS[0];
  } else { // SLEEP
    targetDuty = (wakeUntilMs > millis()) ? LEVELS[0] : 0;
  }

  // 3) Fade currentDuty toward targetDuty at +/- 8 per 30ms.
  unsigned long now = millis();
  if (now - lastFadeMs >= 30) {
    lastFadeMs = now;
    if (currentDuty < targetDuty) {
      uint16_t next = (uint16_t)currentDuty + 8;
      currentDuty = (next > targetDuty) ? targetDuty : (uint8_t)next;
    } else if (currentDuty > targetDuty) {
      int16_t next = (int16_t)currentDuty - 8;
      currentDuty = (next < targetDuty) ? targetDuty : (uint8_t)next;
    }
    if (ledcOk) writeDuty(currentDuty);
  }
}
inline void wake(uint16_t ms) { wakeUntilMs = millis() + ms; }
inline bool isSleeping() { return inQuiet && qhMode == 2; }

inline uint32_t getCycleIntervalMs() {
  switch (cycMode) {
    case 1:  return 60000UL;
    case 2:  return 30000UL;
    case 3:  return 120000UL;
    default: return 0UL;
  }
}

inline void setBrightness(uint8_t lvl) {
  if (lvl > 2) lvl = 2;
  briLevel = lvl;
  if (!inQuiet || qhMode == 0) targetDuty = LEVELS[briLevel];
  Preferences p; p.begin("ohmyclawd", false);
  p.putUChar("bri", briLevel);
  p.end();
}

inline void setQuietHours(uint8_t s, uint8_t e, uint8_t m) {
  qhStart = s % 24;
  qhEnd   = e % 24;
  qhMode  = m % 3;
  Preferences p; p.begin("ohmyclawd", false);
  p.putUChar("qh_s", qhStart);
  p.putUChar("qh_e", qhEnd);
  p.putUChar("qh_m", qhMode);
  p.end();
}

inline void setCycle(uint8_t c) {
  cycMode = c % 4;
  Preferences p; p.begin("ohmyclawd", false);
  p.putUChar("cyc", cycMode);
  p.end();
}

} // namespace display_pm
