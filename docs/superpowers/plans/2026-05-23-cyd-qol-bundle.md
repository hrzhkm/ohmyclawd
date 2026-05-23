# CYD QoL Bundle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add three on-device QoL features to the ohmyclawd CYD firmware — 3-step manual brightness, quiet-hours sleep schedule with touch-wake, and an in-theme offline indicator — all configured from a new SETTINGS mode (4th in the swipe cycle).

**Architecture:** Three new self-contained headers (`display_pm.h`, `offline_ind.h`, `settings_ui.h`) factor the new features out of `main.cpp`. Touch routing in `main.cpp` is extended to dispatch press/hold/tap events into the settings UI when the user is in mode 3. The daemon and the `/usage` wire format are untouched.

**Tech Stack:** PlatformIO + Arduino-ESP32 (core 3.x assumed, with a 2.x ledc fallback), TFT_eSPI, XPT2046_Touchscreen, WiFiManager, Preferences (NVS), C++17.

**Spec reference:** `docs/superpowers/specs/2026-05-22-cyd-qol-bundle-design.md`.

**Test gates:**
- After every task: `pio run -e cyd` must compile cleanly.
- After Task 5 (settings render): a manual hardware smoke test of swiping to SETTINGS is expected.
- After Task 10 (final): all manual test steps in the spec's "Testing" section are expected to pass on the user's device.

---

## File Structure

| File | Status | Responsibility |
|---|---|---|
| `src/display_pm.h` | **create** | Backlight PWM, brightness levels, quiet-hours window, sleep / wake. Owns `BACKLIGHT_PIN`. |
| `src/offline_ind.h` | **create** | Tracks `lastSuccessMs`, computes ONLINE/STALE/OFFLINE state, draws the corner glyph, returns tinted colours. |
| `src/settings_ui.h` | **create** | SETTINGS mode sub-state machine, row rendering, tap handling, RESET hold-fill bar. |
| `src/main.cpp` | modify | Replace direct backlight + 5s-reset paths, add 4th mode dispatch, route touches into settings, apply offline tint in renderers, call `fetchUsage` → `offline_ind::record*`. |
| `README.md` | modify | Document the new SETTINGS mode and the new touch gestures. |

The new headers are written as `inline` / `static` definitions inside their namespaces — no separate `.cpp` files, which keeps the existing single-translation-unit PlatformIO build straightforward.

---

## Task 1: Add `display_pm.h` skeleton and wire it into `setup()`

**Files:**
- Create: `src/display_pm.h`
- Modify: `src/main.cpp` (include + setup)

- [ ] **Step 1: Create `src/display_pm.h` with the full public surface as no-op stubs**

```cpp
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

  currentDuty = 0;
  targetDuty  = LEVELS[briLevel];
  if (ledcOk) writeDuty(currentDuty);
}

inline void tick() { /* implemented in Task 2 */ }
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
```

- [ ] **Step 2: Wire `display_pm::init()` into `main.cpp`'s `setup()` in place of the current backlight write**

Find this block at the top of `setup()` in `src/main.cpp`:

```cpp
  tft.init();
  tft.invertDisplay(true);
  tft.setRotation(0); 
  pinMode(BACKLIGHT_PIN, OUTPUT); digitalWrite(BACKLIGHT_PIN, HIGH);
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI); ts.setRotation(0);
```

Replace it with:

```cpp
  tft.init();
  tft.invertDisplay(true);
  tft.setRotation(0); 
  display_pm::init(prefs);
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI); ts.setRotation(0);
```

Also add the include near the top of `main.cpp`, after `#include "sprite_frames.h"`:

```cpp
#include "display_pm.h"
```

The local `#define BACKLIGHT_PIN 21` in main.cpp can stay — it duplicates the one in the header but doesn't conflict (same value). Leave it untouched for minimal diff.

- [ ] **Step 3: Compile**

Run: `pio run -e cyd`
Expected: success. New header is included but `tick()` is still a no-op; only the boot backlight path is changed (now 0 → no light until Task 2 wires `tick()`).

- [ ] **Step 4: Commit**

```bash
git add src/display_pm.h src/main.cpp
git commit -m "feat(display_pm): add backlight PWM module + wire into setup"
```

---

## Task 2: Implement `display_pm::tick()` with quiet-hours + fade, wire into loop

**Files:**
- Modify: `src/display_pm.h` (replace `tick()` stub)
- Modify: `src/main.cpp` (call `tick()` in `loop()`)

- [ ] **Step 1: Replace the `tick()` stub in `src/display_pm.h`**

```cpp
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
```

- [ ] **Step 2: Call `display_pm::tick()` once per `loop()` iteration**

In `src/main.cpp`'s `loop()`, immediately after the existing touch-handling block (after `delay(200);` and before `if (isAutoCycle && ...)`), insert:

```cpp
  display_pm::tick();
```

- [ ] **Step 3: Compile**

Run: `pio run -e cyd`
Expected: success. On hardware, boot now fades the backlight in from black to the configured level over ~1s.

- [ ] **Step 4: Commit**

```bash
git add src/display_pm.h src/main.cpp
git commit -m "feat(display_pm): implement quiet-hours tick + 1s backlight fade"
```

---

## Task 3: Add `offline_ind.h` + record success/failure from `fetchUsage()`

**Files:**
- Create: `src/offline_ind.h`
- Modify: `src/main.cpp` (include + fetchUsage hooks)

- [ ] **Step 1: Create `src/offline_ind.h`**

```cpp
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

  uint16_t fg = (phase == 0) ? TFT_RED : 0x6800; // dark red
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
```

- [ ] **Step 2: Include the header in `main.cpp`**

Below the existing `#include "display_pm.h"` line, add:

```cpp
#include "offline_ind.h"
```

- [ ] **Step 3: Hook `offline_ind::recordSuccess` / `recordFailure` into `fetchUsage()`**

Replace the body of `fetchUsage()` in `src/main.cpp`:

```cpp
void fetchUsage() {
  HTTPClient http;
  http.begin(daemonUrl + "/usage");
  int code = http.GET();
  if (code == 200) {
    offline_ind::recordSuccess();
    JsonDocument doc; deserializeJson(doc, http.getString());
    usageSession = doc["s"] | 0;
    usageWeekly = doc["w"] | 0;
    usageSR = doc["sr"] | 0;
    usageWR = doc["wr"] | 0;
    claudeWaiting = doc["cw"] | 0;
    // Update sprite based on new status
    if (currentMode == 0 && dynamicSprite) {
      uint8_t newAnim;
      static const uint8_t waitP[] = {3, 4};
      static const uint8_t limitP[] = {2, 6};
      static const uint8_t heavyP[] = {8, 7};
      static const uint8_t modP[] = {12, 11};
      static const uint8_t lightP[] = {0, 1, 9, 10, 5};
      if (claudeWaiting > 0) newAnim = waitP[random(2)];
      else if (usageSession >= 80) newAnim = limitP[random(2)];
      else if (usageSession >= 50) newAnim = heavyP[random(2)];
      else if (usageSession >= 25) newAnim = modP[random(2)];
      else newAnim = lightP[random(5)];
      if (newAnim != spriteAnim) { spriteAnim = newAnim; spriteFrame = 0; }
    }
  } else {
    offline_ind::recordFailure();
  }
  http.end();
}
```

- [ ] **Step 4: Call `offline_ind::update()` once per loop**

In `src/main.cpp`'s `loop()`, right after the `display_pm::tick();` line added in Task 2, add:

```cpp
  offline_ind::update();
```

- [ ] **Step 5: Compile**

Run: `pio run -e cyd`
Expected: success. No visual changes yet (the glyph and tint are not applied to renderers until Task 4).

- [ ] **Step 6: Commit**

```bash
git add src/offline_ind.h src/main.cpp
git commit -m "feat(offline_ind): add state tracking module + fetchUsage hooks"
```

---

## Task 4: Apply offline tint + glyph in `runSprite` / `runClock` / `runSystem`

**Files:**
- Modify: `src/main.cpp` (renderer colour usage + `drawGlyph` calls)

- [ ] **Step 1: Tint the sprite palette in `runSprite()`**

Find this line in `runSprite()`:

```cpp
  static const uint16_t colors[] = {TFT_BLACK, TFT_ORANGE, TFT_BLACK, TFT_CYAN, TFT_DARKGREY, TFT_WHITE};
```

Replace it with:

```cpp
  uint16_t colors[6] = {
    TFT_BLACK,
    offline_ind::tintColor(TFT_ORANGE),
    TFT_BLACK,
    offline_ind::tintColor(TFT_CYAN),
    TFT_DARKGREY,
    TFT_WHITE,
  };
```

- [ ] **Step 2: Tint the usage-bar colours in `runSprite()`**

Find the session bar block:

```cpp
  for (int i = 0; i < numCells; i++) tft.fillRect(barX + i * cellW, sBarY, cellPx, cellPx, (i < sFilled) ? TFT_ORANGE : TFT_DARKGREY);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString(String(usageSession) + "% ", barX + barW + 4, sBarY, 1);
```

Replace with:

```cpp
  uint16_t sessionColor = offline_ind::tintColor(TFT_ORANGE);
  for (int i = 0; i < numCells; i++) tft.fillRect(barX + i * cellW, sBarY, cellPx, cellPx, (i < sFilled) ? sessionColor : TFT_DARKGREY);
  tft.setTextColor(sessionColor, TFT_BLACK);
  tft.drawString(String(usageSession) + "% ", barX + barW + 4, sBarY, 1);
```

Find the weekly bar block:

```cpp
  for (int i = 0; i < numCells; i++) tft.fillRect(barX + i * cellW, wBarY, cellPx, cellPx, (i < wFilled) ? TFT_CYAN : TFT_DARKGREY);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(String(usageWeekly) + "% ", barX + barW + 4, wBarY, 1);
```

Replace with:

```cpp
  uint16_t weeklyColor = offline_ind::tintColor(TFT_CYAN);
  for (int i = 0; i < numCells; i++) tft.fillRect(barX + i * cellW, wBarY, cellPx, cellPx, (i < wFilled) ? weeklyColor : TFT_DARKGREY);
  tft.setTextColor(weeklyColor, TFT_BLACK);
  tft.drawString(String(usageWeekly) + "% ", barX + barW + 4, wBarY, 1);
```

- [ ] **Step 3: Draw the glyph at the end of every renderer**

At the very end of `runSprite()` (before the closing `}`), add:

```cpp
  offline_ind::drawGlyph(tft);
```

At the end of the `if (ti.tm_sec != lsec) {` body inside `runClock()`, **after** the `lsec = ti.tm_sec;` line and before the closing brace, add:

```cpp
    offline_ind::drawGlyph(tft);
```

At the very end of `runSystem()`, before the closing `}`, add:

```cpp
  offline_ind::drawGlyph(tft);
```

- [ ] **Step 4: Compile**

Run: `pio run -e cyd`
Expected: success. On hardware, when WiFi is up and `/usage` is reachable, no visual change. When `/usage` fails for ≥60s, bars / sprite drain to darkgrey; when ≥180s or WiFi disconnects, a pulsing red `X` glyph appears top-right.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat(offline_ind): tint renderers + draw corner X glyph when offline"
```

---

## Task 5: Add `settings_ui.h` skeleton + BROWSE-mode render, register 4th mode

**Files:**
- Create: `src/settings_ui.h`
- Modify: `src/main.cpp` (include + `runSettings()` + switch dispatch + swipe `% 4`)

- [ ] **Step 1: Create `src/settings_ui.h`**

```cpp
// src/settings_ui.h
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include "display_pm.h"

namespace settings_ui {

enum Sub { BROWSE, EDIT_QH_START, EDIT_QH_END };

static Sub           sub               = BROWSE;
static bool          needsFullRedraw   = true;
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

inline void render(TFT_eSPI& tft, bool fullRedraw);

inline void enter() {
  sub = BROWSE;
  needsFullRedraw = true;
  resetActive = false;
  resetFilledPx = 0;
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
}

} // namespace settings_ui
```

- [ ] **Step 2: Include settings_ui in `main.cpp`**

Below `#include "offline_ind.h"`, add:

```cpp
#include "settings_ui.h"
```

- [ ] **Step 3: Add a `runSettings()` forward declaration and definition**

In the forward-declarations block near the top of `main.cpp`:

```cpp
void nextMode();
void runSprite();
void runClock();
void runSystem();
void fetchUsage();
void checkOTA();
```

Add a new line:

```cpp
void runSettings();
```

Then, just above the existing `void nextMode()` *definition* further down in the file, add the function body:

```cpp
void runSettings() {
  if (modeChanged) { settings_ui::enter(); modeChanged = false; settings_ui::render(tft, true); return; }
  settings_ui::render(tft, false);
}
```

In the `loop()` function, find this switch:

```cpp
  switch (currentMode) {
    case 0: runSprite(); break;
    case 1: runClock(); break;
    case 2: runSystem(); break;
  }
```

Replace it with:

```cpp
  switch (currentMode) {
    case 0: runSprite(); break;
    case 1: runClock(); break;
    case 2: runSystem(); break;
    case 3: runSettings(); break;
  }
```

- [ ] **Step 4: Update the swipe handler to cycle 0..3**

In `loop()`, find the swipe branch:

```cpp
    if (elapsed < 500 && abs(deltaX) > 40) {
      // Swipe
      isAutoCycle = false;
      if (deltaX > 0) { currentMode = (currentMode + 2) % 3; } // swipe right = prev
      else { currentMode = (currentMode + 1) % 3; }            // swipe left = next
      modeChanged = true; modeTimer = millis(); tft.fillScreen(TFT_BLACK);
    } else if (elapsed < 300 && abs(deltaX) < 20) {
```

Replace the swipe arithmetic to use 4 modes and also exit any settings edit sub-mode:

```cpp
    if (elapsed < 500 && abs(deltaX) > 40) {
      // Swipe
      isAutoCycle = false;
      if (currentMode == 3) settings_ui::exit();
      if (deltaX > 0) { currentMode = (currentMode + 3) % 4; } // swipe right = prev (of 4)
      else { currentMode = (currentMode + 1) % 4; }            // swipe left  = next (of 4)
      modeChanged = true; modeTimer = millis(); tft.fillScreen(TFT_BLACK);
    } else if (elapsed < 300 && abs(deltaX) < 20) {
```

- [ ] **Step 5: Compile**

Run: `pio run -e cyd`
Expected: success.

- [ ] **Step 6: (Hardware smoke check)**

If the device is available, flash and verify swiping reaches a "SETTINGS" screen with all five rows visible. Values still don't change on tap (handled in Task 6); RESET hold doesn't fire (handled in Task 8). This is purely a visual smoke check.

- [ ] **Step 7: Commit**

```bash
git add src/settings_ui.h src/main.cpp
git commit -m "feat(settings_ui): add SETTINGS mode skeleton + 4th mode dispatch"
```

---

## Task 6: Implement `handleTap` for rows 0, 2, 3 + wire into touch dispatch

**Files:**
- Modify: `src/settings_ui.h` (add `handleTap`)
- Modify: `src/main.cpp` (touch dispatch when `currentMode == 3`)

- [ ] **Step 1: Add `handleTap` to `settings_ui.h`**

Add this function inside the `namespace settings_ui { ... }` block, **above** the `inline void render(...)` definition:

```cpp
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
      break;
    }
    case 1: {
      if (sub == BROWSE) {
        sub = EDIT_QH_START;
        subEnteredMs = millis();
        needsFullRedraw = true;
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
      }
      break;
    }
    case 2: {
      display_pm::setQuietHours(display_pm::qhStart, display_pm::qhEnd, (display_pm::qhMode + 1) % 3);
      flashRow(tft, 2, "QUIET MODE", String(qhmLabel()));
      needsFullRedraw = true;
      break;
    }
    case 3: {
      display_pm::setCycle((display_pm::cycMode + 1) % 4);
      flashRow(tft, 3, "AUTO-CYCLE", String(cycLabel()));
      needsFullRedraw = true;
      break;
    }
    case 4:
      // Brief tap on RESET = no-op. Only hold triggers.
      break;
  }
}
```

- [ ] **Step 2: Dispatch taps into `settings_ui::handleTap` from `main.cpp`**

Find the tap branch in `loop()`:

```cpp
    } else if (elapsed < 300 && abs(deltaX) < 20) {
      // Tap on sprite mode
      if (currentMode == 0) {
        // ... existing double-tap / single-tap logic ...
      }
    }
```

Replace it with:

```cpp
    } else if (elapsed < 300 && abs(deltaX) < 20) {
      int tapY = map(p.y, 300, 3900, 0, 320);
      if (currentMode == 3) {
        settings_ui::handleTap(tft, tapY);
      } else if (currentMode == 0) {
        if (millis() - lastTapTime < 400) {
          // Double tap: toggle dynamic mode
          dynamicSprite = !dynamicSprite;
          lastTapTime = 0;
          // Flash message in fun word area
          tft.fillRect(0, 35, 240, 10, TFT_BLACK);
          tft.setTextSize(1); tft.setTextColor(TFT_ORANGE, TFT_BLACK);
          tft.setTextDatum(MC_DATUM);
          tft.drawString(dynamicSprite ? "dynamic mode" : "free mode", 120, 39, 1);
          tft.setTextDatum(TL_DATUM);
        } else {
          // Single tap: cycle sprite
          lastTapTime = millis();
          if (dynamicSprite) {
            // Cycle within current state pool
            static const uint8_t waitPool[] = {3, 4};
            static const uint8_t limitPool[] = {2, 6};
            static const uint8_t heavyPool[] = {8, 7};
            static const uint8_t modPool[] = {12, 11};
            static const uint8_t lightPool[] = {0, 1, 9, 10, 5};
            const uint8_t* pool; uint8_t poolSize;
            if (claudeWaiting > 0) { pool = waitPool; poolSize = 2; }
            else if (usageSession >= 80) { pool = limitPool; poolSize = 2; }
            else if (usageSession >= 50) { pool = heavyPool; poolSize = 2; }
            else if (usageSession >= 25) { pool = modPool; poolSize = 2; }
            else { pool = lightPool; poolSize = 5; }
            uint8_t idx = 0;
            for (uint8_t i = 0; i < poolSize; i++) { if (pool[i] == spriteAnim) { idx = i; break; } }
            spriteAnim = pool[(idx + 1) % poolSize];
          } else {
            // Free cycle through all sprites
            spriteAnim = (spriteAnim + 1) % SPRITE_ANIM_COUNT;
          }
          spriteFrame = 0;
        }
      }
    }
```

- [ ] **Step 3: Compile**

Run: `pio run -e cyd`
Expected: success. On hardware, in SETTINGS: tap BRIGHTNESS → label changes immediately and PWM follows. Tap QUIET MODE / AUTO-CYCLE → labels cycle.

- [ ] **Step 4: Commit**

```bash
git add src/settings_ui.h src/main.cpp
git commit -m "feat(settings_ui): tap-cycle brightness, quiet-mode, auto-cycle rows"
```

---

## Task 7: Implement quiet-hours edit submode auto-commit timeout

**Files:**
- Modify: `src/settings_ui.h` (auto-commit logic in `render`)

The chevron logic is already in place from Task 6. This task adds the 3-second auto-commit timer so the user isn't stuck in an edit submode if they tap once and walk away.

- [ ] **Step 1: Update the render entry to check for timeout**

Add the following near the top of the `render(...)` body in `settings_ui.h`, **before** the `if (fullRedraw || needsFullRedraw)` block:

```cpp
  if ((sub == EDIT_QH_START || sub == EDIT_QH_END) &&
      millis() - subEnteredMs > EDIT_TIMEOUT_MS) {
    sub = (sub == EDIT_QH_START) ? EDIT_QH_END : BROWSE;
    subEnteredMs = millis();
    needsFullRedraw = true;
  }
```

Note: stepping from `EDIT_QH_START` to `EDIT_QH_END` on timeout matches the natural progression the user takes when tapping their way through the editor.

- [ ] **Step 2: Compile**

Run: `pio run -e cyd`
Expected: success. On hardware, tap QUIET HOURS once → enter edit start; wait 3s without tapping → auto-advance to edit end; wait another 3s → return to BROWSE.

- [ ] **Step 3: Commit**

```bash
git add src/settings_ui.h
git commit -m "feat(settings_ui): auto-commit quiet-hours edit submode after 3s idle"
```

---

## Task 8: Implement RESET hold-fill, suppress global 5s reset in SETTINGS

**Files:**
- Modify: `src/settings_ui.h` (`handleHoldTick`, factory-reset trigger)
- Modify: `src/main.cpp` (touch-loop hold dispatch + suppression)

- [ ] **Step 1: Add `handleHoldTick` and `consumeResetIfTriggered` to `settings_ui.h`**

Insert these inside the namespace, above the `render(...)` definition:

```cpp
static constexpr unsigned long RESET_HOLD_MS = 3000UL;

inline void handleHoldTick(TFT_eSPI& tft, int touchY, unsigned long elapsedMs) {
  int row = rowFromY(touchY);
  if (row != 4) {
    if (resetActive) {
      resetActive = false;
      resetFilledPx = 0;
      needsFullRedraw = true;
    }
    return;
  }

  if (!resetActive) {
    resetActive = true;
    resetHoldStartMs = millis() - elapsedMs;
  }

  unsigned long held = millis() - resetHoldStartMs;
  if (held > RESET_HOLD_MS) held = RESET_HOLD_MS;
  int filled = (int)((held * 240UL) / RESET_HOLD_MS);
  if (filled != resetFilledPx) {
    resetFilledPx = filled;
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
  }
}

inline bool consumeResetIfTriggered() {
  if (resetActive && (millis() - resetHoldStartMs) >= RESET_HOLD_MS) {
    resetActive = false;
    resetFilledPx = 0;
    return true;
  }
  return false;
}

inline void cancelHold() {
  if (resetActive) {
    resetActive = false;
    resetFilledPx = 0;
    needsFullRedraw = true;
  }
}
```

- [ ] **Step 2: Dispatch hold + suppress global reset in `main.cpp`**

Find this block inside the touch loop:

```cpp
    while (ts.touched()) {
      if (millis() - touchStart >= 5000) {
        WiFiManager wm;
        wm.resetSettings();
        prefs.begin("ohmyclawd", false); prefs.clear(); prefs.end();
        ESP.restart();
      }
      p = ts.getPoint();
      lastX = map(p.x, 300, 3900, 0, 240);
      delay(20);
    }
```

Replace it with:

```cpp
    while (ts.touched()) {
      unsigned long elapsedSoFar = millis() - touchStart;
      if (currentMode == 3) {
        int yMapped = map(p.y, 300, 3900, 0, 320);
        settings_ui::handleHoldTick(tft, yMapped, elapsedSoFar);
        if (settings_ui::consumeResetIfTriggered()) {
          WiFiManager wm;
          wm.resetSettings();
          prefs.begin("ohmyclawd", false); prefs.clear(); prefs.end();
          ESP.restart();
        }
      } else {
        if (elapsedSoFar >= 5000) {
          WiFiManager wm;
          wm.resetSettings();
          prefs.begin("ohmyclawd", false); prefs.clear(); prefs.end();
          ESP.restart();
        }
      }
      p = ts.getPoint();
      lastX = map(p.x, 300, 3900, 0, 240);
      delay(20);
    }
```

- [ ] **Step 3: Cancel the hold on release**

After the existing `unsigned long elapsed = millis() - touchStart;` line in `loop()`, add:

```cpp
    if (currentMode == 3) settings_ui::cancelHold();
```

- [ ] **Step 4: Compile**

Run: `pio run -e cyd`
Expected: success. On hardware, hold the RESET row 3s → bar fills L→R → device restarts and re-enters the captive portal. Hold the RESET row 2.5s and release → bar resets, nothing happens. Hold the SETTINGS screen for 5s outside row 4 → no factory reset (the global path is suppressed).

- [ ] **Step 5: Commit**

```bash
git add src/settings_ui.h src/main.cpp
git commit -m "feat(settings_ui): 3s hold-to-reset with fill bar, suppress global 5s in mode 3"
```

---

## Task 9: Sleep-mode touch short-circuit + auto-cycle uses `getCycleIntervalMs`

**Files:**
- Modify: `src/main.cpp` (touch entry + auto-cycle interval lookup)

- [ ] **Step 1: Short-circuit touch when sleeping**

At the very top of the `if (ts.touched()) { ... }` block in `loop()`, **before** the `TS_Point p = ts.getPoint();` line, insert:

```cpp
    if (display_pm::isSleeping()) {
      display_pm::wake(10000);
      // Drain the rest of this press so it doesn't fall through to gesture handling.
      while (ts.touched()) { delay(20); }
      delay(200);
      return;
    }
```

The `return;` exits `loop()` early; the next iteration picks up normally.

- [ ] **Step 2: Replace hard-coded auto-cycle interval**

Find this line in `loop()`:

```cpp
  if (isAutoCycle && (millis() - modeTimer > interval)) nextMode();
```

Replace it with:

```cpp
  uint32_t cycleMs = display_pm::getCycleIntervalMs();
  if (isAutoCycle && cycleMs > 0 && (millis() - modeTimer > cycleMs)) nextMode();
```

The old `const unsigned long interval = 60000;` global at the top of `main.cpp` becomes unused. Delete that declaration to keep the file tidy.

- [ ] **Step 3: Compile**

Run: `pio run -e cyd`
Expected: success. On hardware: when SLEEP quiet-mode is active and the window matches, the backlight is off; tapping wakes it to LOW for 10s. AUTO-CYCLE OFF stops mode rotation; 30s and 120s adjust the rotation speed.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat(display_pm): touch-wake while sleeping + dynamic auto-cycle interval"
```

---

## Task 10: README update + final smoke compile

**Files:**
- Modify: `README.md`
- Verify: `pio run -e cyd`

- [ ] **Step 1: Update `README.md`'s "Features" list**

Find the existing "Features" section:

```markdown
## Features

- **Real-time usage bars** — session and weekly utilization at a glance
- **13 animated pixel sprites** — changes based on Claude Code activity state
- **Tmux session detection** — knows when Claude is waiting for your input
- **OTA firmware updates** — checks GitHub releases on boot, tap to update
- **Configurable via captive portal** — no code changes needed for WiFi/daemon setup
- **Pixel clock mode** — retro digital clock with second-progress bar
```

Replace it with:

```markdown
## Features

- **Real-time usage bars** — session and weekly utilization at a glance
- **13 animated pixel sprites** — changes based on Claude Code activity state
- **Tmux session detection** — knows when Claude is waiting for your input
- **OTA firmware updates** — checks GitHub releases on boot, tap to update
- **Configurable via captive portal** — no code changes needed for WiFi/daemon setup
- **Pixel clock mode** — retro digital clock with second-progress bar
- **On-device settings** — brightness, quiet hours, auto-cycle, and factory reset from a SETTINGS mode (swipe to reach)
- **Offline indicator** — pixel `X` glyph and colour-drain when the daemon or Wi-Fi is unreachable
```

- [ ] **Step 2: Add a SETTINGS section after the existing "Sprite States" table**

Append after the Sprite States section:

```markdown
## On-device settings

Swipe through modes until you reach **SETTINGS**. Auto-cycle (60s by default) does NOT visit settings — it has to be reached manually.

| Row | Tap behaviour |
|---|---|
| BRIGHTNESS | cycles `LOW` / `MID` / `HIGH` — backlight PWM applies instantly |
| QUIET HOURS | tap once to edit start hour, again to edit end hour (chevron taps adjust) |
| QUIET MODE | `OFF` / `DIM` / `SLEEP` — DIM uses the lowest brightness during the window, SLEEP turns the backlight off |
| AUTO-CYCLE | `OFF` / `60s` / `30s` / `120s` — controls the rotation interval for the other three modes |
| RESET | press and hold 3 seconds; the orange bar fills L→R, then the device clears Wi-Fi credentials and reboots into the captive portal |

While quiet hours is in `SLEEP` mode, the screen is dark. Tap to wake the backlight to `LOW` for 10 seconds.

When the daemon or Wi-Fi is unreachable, the usage bars and sprite drain to dark grey, and a pulsing red `X` appears top-right. Full colour returns automatically when the daemon is reachable again.
```

- [ ] **Step 3: Final compile**

Run: `pio run -e cyd`
Expected: success. Note the new sizes in the build summary — the bundle should add roughly 6–10 KB of flash and a few hundred bytes of RAM.

- [ ] **Step 4: Commit**

```bash
git add README.md
git commit -m "docs: document on-device SETTINGS mode + offline indicator"
```

- [ ] **Step 5: Run a final `git log` summary**

Run: `git log --oneline master..HEAD`
Expected: 11 commits (1 spec + 10 implementation tasks).

---

## Self-Review Checklist (already applied)

- [x] Spec coverage: every spec section maps to a task (brightness → 1+2, offline tint+glyph → 3+4, settings render → 5, taps → 6+7, hold-reset → 8, sleep + cycle interval → 9, docs → 10).
- [x] No placeholders: every code step has the literal code to write.
- [x] Type consistency: function names match across tasks (`handleTap`, `handleHoldTick`, `consumeResetIfTriggered`, `cancelHold`, `enter`, `exit`, `render`, `recordSuccess`, `recordFailure`, `update`, `tintColor`, `drawGlyph`, `init`, `tick`, `wake`, `isSleeping`, `getCycleIntervalMs`, `setBrightness`, `setQuietHours`, `setCycle`).
- [x] NVS keys consistent: `bri`, `qh_s`, `qh_e`, `qh_m`, `cyc` everywhere.
- [x] Compile gate after every task; manual hardware gate after Task 5.
