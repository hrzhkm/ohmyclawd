// src/offline_ind.h
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>

namespace offline_ind {

enum State { ONLINE, STALE, OFFLINE };

// Thresholds (ms).
static constexpr unsigned long STALE_AFTER_MS   = 60UL  * 1000UL;
static constexpr unsigned long OFFLINE_AFTER_MS = 180UL * 1000UL;
static constexpr unsigned long PULSE_PERIOD_MS  = 500UL;

// Glyph colours.
static constexpr uint16_t GLYPH_DARK_RED = 0x6800;

static State         currentState   = OFFLINE; // until first success
static unsigned long lastSuccessMs  = 0;
static bool          hadSuccessEver = false;
static State         lastDrawnState = ONLINE;  // forces first redraw
static uint8_t       lastPulsePhase = 2;       // 0/1 actual; 2 = "unknown"

// 5x5 cell offline glyph — diagonal X.
static const uint8_t GLYPH[5][5] = {
  {1, 0, 0, 0, 1},
  {0, 1, 0, 1, 0},
  {0, 0, 1, 0, 0},
  {0, 1, 0, 1, 0},
  {1, 0, 0, 0, 1},
};
static constexpr int GLYPH_X      = 200;
static constexpr int GLYPH_Y      = 5;
static constexpr int GLYPH_CELL   = 8;
static constexpr int GLYPH_PX     = 7;
static constexpr int GLYPH_SIDE   = 5 * GLYPH_CELL;

inline void recordSuccess() {
  lastSuccessMs  = millis();
  hadSuccessEver = true;
}

inline void recordFailure() {
  // Nothing to record beyond "we tried"; staleness is derived from lastSuccessMs.
  // Kept as an explicit hook so callers express intent.
  (void)0;
}

inline State update() {
  if (WiFi.status() != WL_CONNECTED) {
    currentState = OFFLINE;
    return currentState;
  }
  if (!hadSuccessEver) {
    currentState = OFFLINE;
    return currentState;
  }
  unsigned long age = millis() - lastSuccessMs;
  if (age >= OFFLINE_AFTER_MS) currentState = OFFLINE;
  else if (age >= STALE_AFTER_MS) currentState = STALE;
  else currentState = ONLINE;
  return currentState;
}

inline State state() { return currentState; }

inline uint16_t tintColor(uint16_t base) {
  return (currentState == ONLINE) ? base : TFT_DARKGREY;
}

inline bool stateChangedSinceLastDraw() {
  return currentState != lastDrawnState;
}

inline void drawGlyph(TFT_eSPI& tft) {
  uint8_t phase = (millis() / PULSE_PERIOD_MS) & 1;
  bool stateChanged = (currentState != lastDrawnState);
  bool phaseChanged = (phase != lastPulsePhase);

  if (currentState != OFFLINE) {
    if (stateChanged) {
      // Erase any previously drawn glyph cells.
      tft.fillRect(GLYPH_X, GLYPH_Y, GLYPH_SIDE, GLYPH_SIDE, TFT_BLACK);
      lastDrawnState = currentState;
      lastPulsePhase = 2;
    }
    return;
  }

  if (!stateChanged && !phaseChanged) return;

  uint16_t fg = (phase == 0) ? TFT_RED : GLYPH_DARK_RED;
  for (int r = 0; r < 5; r++) {
    for (int c = 0; c < 5; c++) {
      uint16_t color = GLYPH[r][c] ? fg : TFT_BLACK;
      tft.fillRect(GLYPH_X + c * GLYPH_CELL, GLYPH_Y + r * GLYPH_CELL,
                   GLYPH_PX, GLYPH_PX, color);
    }
  }
  lastDrawnState = currentState;
  lastPulsePhase = phase;
}

} // namespace offline_ind
