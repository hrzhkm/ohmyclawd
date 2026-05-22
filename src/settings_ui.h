// src/settings_ui.h
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "display_pm.h"

namespace settings_ui {

enum Sub { BROWSE, EDIT_QH_START, EDIT_QH_END };

static Sub           sub               = BROWSE;
static bool          needsFullRedraw   = true;
static bool          rowsDirty         = true;
static unsigned long resetHoldStartMs  = 0;
static bool          resetActive       = false;
static int           resetFilledPx     = 0;
static unsigned long subEnteredMs      = 0;
static const unsigned long EDIT_TIMEOUT_MS = 3000UL;

// Layout constants.
static constexpr int HEADER_Y_TOP    = 5;
static constexpr int SUBHEADER_Y     = 30;
static constexpr int ROW0_Y          = 50;
static constexpr int ROW_H           = 45;
static constexpr int LABEL_X         = 15;
static constexpr int VALUE_X         = 225;
static constexpr int RULE_INDENT     = 5;
static constexpr int NUM_ROWS        = 5;

inline int rowY(int idx) { return ROW0_Y + idx * ROW_H; }

inline int rowFromY(int y) {
  if (y < ROW0_Y) return -1;
  int idx = (y - ROW0_Y) / ROW_H;
  if (idx >= NUM_ROWS) return -1;
  return idx;
}

inline const char* briLabel() {
  switch (display_pm::briLevel) { case 0: return "LOW"; case 1: return "MID"; default: return "HIGH"; }
}
inline const char* qhmLabel() {
  switch (display_pm::qhMode) { case 0: return "OFF"; case 1: return "DIM"; default: return "SLEEP"; }
}
inline const char* cycLabel() {
  switch (display_pm::cycMode) { case 0: return "OFF"; case 1: return "60s"; case 2: return "30s"; default: return "120s"; }
}

inline String qhRangeStr() {
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:00 -> %02d:00", display_pm::qhStart, display_pm::qhEnd);
  return String(buf);
}

inline void drawRow(TFT_eSPI& tft, int idx, const char* label, const String& value, bool highlighted) {
  int y = rowY(idx);
  uint16_t bg = highlighted ? TFT_ORANGE : TFT_BLACK;
  uint16_t labelFg = highlighted ? TFT_BLACK : TFT_DARKGREY;
  uint16_t valueFg = highlighted ? TFT_BLACK : TFT_ORANGE;
  tft.fillRect(0, y, 240, ROW_H - 1, bg);
  tft.setTextSize(1);
  tft.setTextColor(labelFg, bg);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(label, LABEL_X, y + 8, 2);
  tft.setTextColor(valueFg, bg);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(value, VALUE_X, y + 8, 2);
  tft.drawFastHLine(RULE_INDENT, y + ROW_H - 1, 240 - 2 * RULE_INDENT, TFT_DARKGREY);
}

inline void flashRow(TFT_eSPI& tft, int idx, const char* label, const String& value) {
  drawRow(tft, idx, label, value, true);
  delay(120);
}

inline void handleTap(TFT_eSPI& tft, int touchY) {
  int row = rowFromY(touchY);
  if (row < 0) return;

  // Any tap outside row 4 cancels an in-progress hold visualisation.
  if (row != 4) {
    resetActive = false;
    resetFilledPx = 0;
  }

  switch (row) {
    case 0: {
      display_pm::setBrightness((display_pm::briLevel + 1) % 3);
      flashRow(tft, 0, "BRIGHTNESS", String(briLabel()));
      needsFullRedraw = true;
      rowsDirty = true;
      break;
    }
    case 1: {
      if (sub == BROWSE) {
        sub = EDIT_QH_START;
        subEnteredMs = millis();
        needsFullRedraw = true;
        rowsDirty = true;
      } else if (sub == EDIT_QH_START || sub == EDIT_QH_END) {
        // Chevron handling — top half of row = +1, bottom half = -1.
        int y = rowY(1);
        int midY = y + ROW_H / 2;
        uint8_t& target = (sub == EDIT_QH_START) ? display_pm::qhStart : display_pm::qhEnd;
        if (touchY < midY) target = (target + 1) % 24;
        else                target = (target + 23) % 24;
        display_pm::setQuietHours(display_pm::qhStart, display_pm::qhEnd, display_pm::qhMode);
        subEnteredMs = millis();
        needsFullRedraw = true;
        rowsDirty = true;
      }
      break;
    }
    case 2: {
      display_pm::setQuietHours(display_pm::qhStart, display_pm::qhEnd, (display_pm::qhMode + 1) % 3);
      flashRow(tft, 2, "QUIET MODE", String(qhmLabel()));
      needsFullRedraw = true;
      rowsDirty = true;
      break;
    }
    case 3: {
      display_pm::setCycle((display_pm::cycMode + 1) % 4);
      flashRow(tft, 3, "AUTO-CYCLE", String(cycLabel()));
      needsFullRedraw = true;
      rowsDirty = true;
      break;
    }
    case 4:
      // Brief tap on RESET = no-op. Only hold triggers (Task 8 handles).
      break;
  }
}

inline void render(TFT_eSPI& tft, bool fullRedraw);

inline void enter() {
  sub = BROWSE;
  needsFullRedraw = true;
  rowsDirty = true;
  resetActive = false;
  resetFilledPx = 0;
  subEnteredMs = millis();
}

inline void exit() {
  resetActive = false;
  sub = BROWSE;
}

inline void render(TFT_eSPI& tft, bool fullRedraw) {
  if (fullRedraw || needsFullRedraw) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("SETTINGS", 120, HEADER_Y_TOP + 10, 1);
    tft.setTextSize(1);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("swipe to exit", 120, SUBHEADER_Y + 4, 1);
    tft.setTextDatum(TL_DATUM);
    needsFullRedraw = false;
  }

  if (rowsDirty || fullRedraw) {
    // Row 0 - BRIGHTNESS
    drawRow(tft, 0, "BRIGHTNESS", String(briLabel()), false);

    // Row 1 - QUIET HOURS (varies with sub-mode)
    if (sub == EDIT_QH_START) {
      char buf[16];
      snprintf(buf, sizeof(buf), "< %02d >", display_pm::qhStart);
      drawRow(tft, 1, "QUIET START", String(buf), true);
    } else if (sub == EDIT_QH_END) {
      char buf[16];
      snprintf(buf, sizeof(buf), "< %02d >", display_pm::qhEnd);
      drawRow(tft, 1, "QUIET END", String(buf), true);
    } else {
      drawRow(tft, 1, "QUIET HOURS", qhRangeStr(), false);
    }

    drawRow(tft, 2, "QUIET MODE", String(qhmLabel()), false);
    drawRow(tft, 3, "AUTO-CYCLE", String(cycLabel()), false);

    // Row 4 - RESET (paints fill bar if active)
    if (resetActive) {
      int y = rowY(4);
      tft.fillRect(0, y, 240, ROW_H - 1, TFT_BLACK);
      tft.fillRect(0, y, resetFilledPx, ROW_H - 1, TFT_ORANGE);
      tft.setTextSize(1);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.setTextDatum(TL_DATUM);
      tft.drawString("RESET", LABEL_X, y + 8, 2);
      tft.setTextDatum(TR_DATUM);
      tft.drawString("hold...", VALUE_X, y + 8, 2);
      tft.drawFastHLine(RULE_INDENT, y + ROW_H - 1, 240 - 2 * RULE_INDENT, TFT_DARKGREY);
    } else {
      drawRow(tft, 4, "RESET", "hold 3s", false);
    }

    rowsDirty = false;
  }
}

} // namespace settings_ui
