# CYD QoL Bundle Design

**Status:** approved, ready for plan
**Date:** 2026-05-22
**Scope:** firmware-only (no daemon changes)

## Summary

A bundle of three quality-of-life features for the ohmyclawd CYD firmware that share a small amount of infrastructure (NVS prefs, backlight PWM, touch routing, mode cycle):

1. **Manual brightness control** — three-step backlight PWM (low / mid / high), no light sensor.
2. **Sleep schedule** — configurable quiet-hours window with three behaviour modes (off / dim / sleep) and touch-wake.
3. **In-theme offline indicator** — pixel-cell glyph plus stale-data colour drain that fits the existing retro aesthetic.

All three are configured from a new fourth on-device mode, **SETTINGS**, reached by swiping. No reflash needed to change behaviour.

## Goals

- Keep ohmyclawd feeling alive and "aware" of its environment without adding hardware dependencies (LDR is skipped on purpose — the user prefers predictable manual control).
- Provide a quiet-hours model that suits a desk-side device left on overnight.
- Surface offline / stale state with a glyph that matches the existing 7-px-in-8-px-cell pixel aesthetic, not a generic banner.
- Settings are CYD-device-specific only: no firmware version row, no Wi-Fi URL row, no timezone row. Those remain captive-portal-only.

## Non-goals

- LDR-based auto-brightness (explicitly rejected).
- Inactivity-based sleep (only time-window sleep is in scope).
- Daemon-side configuration UI.
- Multi-host or multi-machine aggregation.
- Mini-games, achievements, tamagotchi, or other "fun" ideas — those stay in the brainstorm backlog.

## Architecture

Three new header files, one new mode, one new touch-routing branch. Daemon untouched.

```
src/
├── main.cpp           # setup, loop, mode dispatch, touch routing
├── settings_ui.h      # SETTINGS mode render + touch handler
├── display_pm.h       # backlight PWM, brightness levels, quiet-hours check
├── offline_ind.h      # stale/offline state + pixel glyph render
└── sprite_frames.h    # unchanged
```

Each new header is self-contained with `inline` / `static` functions to keep PlatformIO single-translation-unit assumptions happy and keep `main.cpp` small.

### Module responsibilities

| Module | Owns | Public surface |
|---|---|---|
| `display_pm` | `BACKLIGHT_PIN`, brightness state, quiet-hours window | `init`, `setBrightness`, `setQuietHours`, `setCycle`, `wake`, `tick`, `isSleeping`, `getCycleIntervalMs` |
| `offline_ind` | `lastSuccessMs`, current state | `recordSuccess`, `recordFailure`, `update`, `tintColor`, `drawGlyph`, `state` |
| `settings_ui` | settings sub-mode state machine | `enter`, `exit`, `render`, `handleTap`, `handleHoldTick` |

### Data flow

1. `loop()` calls `display_pm::tick()` every iteration — adjusts backlight if entering / exiting quiet window or fading.
2. `fetchUsage()` calls `offline_ind::recordSuccess()` on HTTP 200 and `recordFailure()` otherwise.
3. `runSprite()` / `runClock()` / `runSystem()` consult `offline_ind::tintColor(base)` for the orange / cyan they would normally use, and call `offline_ind::drawGlyph(tft, x, y)` for the corner indicator.
4. Mode cycle becomes 4 entries; auto-cycle (`nextMode()`) loops through 3 only (skips SETTINGS).
5. Touch routing in `loop()` branches: while a touch is held, `settings_ui::handleHoldTick(...)` is called during the press loop when `currentMode == 3`; on release, taps in settings dispatch to `settings_ui::handleTap(...)` and taps elsewhere fall through to the existing tap / double-tap logic. Swipes are always handled globally.
6. While `display_pm::isSleeping()`, the touch handler short-circuits to `display_pm::wake(10000)` and returns — touches during sleep don't fire taps / swipes.

### NVS schema

Namespace `ohmyclawd` (already in use). Existing keys kept.

| Key | Type | Default | Purpose |
|---|---|---|---|
| `url` | String | `http://ohmyclawd.local:8787` | (existing) |
| `tz` | String | `UTC-8` | (existing) |
| `bri` | uchar | `2` (HIGH) | brightness level: 0=LOW, 1=MID, 2=HIGH |
| `qh_s` | uchar | `23` | quiet-hours start, 0..23 |
| `qh_e` | uchar | `7` | quiet-hours end, 0..23 |
| `qh_m` | uchar | `0` | quiet mode: 0=OFF, 1=DIM, 2=SLEEP |
| `cyc` | uchar | `1` | auto-cycle: 0=OFF, 1=60s, 2=30s, 3=120s |

Existing devices will hit defaults the first time the new firmware boots — no migration logic required.

## display_pm.h — brightness + quiet hours

### Constants and state

```cpp
namespace display_pm {
  // 8-bit ledc duty values for each level.
  // LOW=64 (~25%), MID=160 (~62%), HIGH=255 (100%)
  constexpr uint8_t LEVELS[3] = {64, 160, 255};
  constexpr int LEDC_CHANNEL = 0;
  constexpr int LEDC_FREQ_HZ = 5000;
  constexpr int LEDC_RES_BITS = 8;

  // ...state vars...
  uint8_t briLevel;
  uint8_t qhStart, qhEnd;
  uint8_t qhMode;
  uint8_t cycMode;
  bool inQuiet;
  unsigned long wakeUntilMs;
  uint8_t currentDuty;     // last value written, for fade smoothing
  uint8_t targetDuty;
  unsigned long lastFadeMs;
}
```

### `init()`

- `ledcAttachChannel(BACKLIGHT_PIN, LEDC_FREQ_HZ, LEDC_RES_BITS, LEDC_CHANNEL)` — replaces the existing `pinMode + digitalWrite(BACKLIGHT_PIN, HIGH)` in `setup()`.
- Reads `bri`, `qh_s`, `qh_e`, `qh_m`, `cyc` from NVS via the shared `prefs` handle.
- Sets `currentDuty = 0` and `targetDuty = LEVELS[briLevel]`. The first run of `tick()` will fade the backlight in from black to the configured level over roughly one second, giving the boot a smooth power-on feel instead of a hard flash.

### `tick()`

Called every `loop()` iteration. Cheap path: most calls are no-ops.

1. If `getLocalTime(&ti)` fails, treat as `inQuiet = false` and return early.
2. Compute `nowInWindow`:
   - If `qhStart < qhEnd`: same-day window. `inside = hour >= qhStart && hour < qhEnd`.
   - If `qhStart >= qhEnd`: cross-midnight. `inside = hour >= qhStart || hour < qhEnd`.
   - If `qhStart == qhEnd`: window is disabled. `inside = false`.
3. Determine `targetDuty`:
   - Not in quiet, or `qhMode==0` (OFF): `targetDuty = LEVELS[briLevel]`.
   - In quiet, `qhMode==1` (DIM): `targetDuty = LEVELS[0]`.
   - In quiet, `qhMode==2` (SLEEP): if `wakeUntilMs > millis()`, `targetDuty = LEVELS[0]`; else `targetDuty = 0`.
4. Fade `currentDuty` toward `targetDuty` by ±8 every 30 ms (gives roughly a 1-second full-range fade). Write the new value via `ledcWrite(LEDC_CHANNEL, currentDuty)`.

### `wake(uint16_t ms)`

Called by the touch handler when a touch is detected while `isSleeping()` is true. Sets `wakeUntilMs = millis() + ms`. Subsequent touches during the wake window extend it.

### `isSleeping()`

True whenever the device is in the sleep-mode cycle: `inQuiet && qhMode == 2`. Used by `main.cpp` touch handler to short-circuit tap / swipe / double-tap processing — during the sleep cycle, touches only ever extend the wake window, never trigger normal interactions. The user must wait for quiet hours to end (or the wake window to elapse and a fresh touch within the wake window to keep the screen lit at LOW) before normal touch interactions resume.

### `getCycleIntervalMs()`

Returns the auto-cycle interval based on `cycMode`: 0→0 (disabled), 1→60000, 2→30000, 3→120000.

### Cross-midnight test cases

| qhStart | qhEnd | Now (hour) | Expected |
|---|---|---|---|
| 23 | 7 | 2 | inside |
| 23 | 7 | 12 | outside |
| 23 | 7 | 23 | inside |
| 23 | 7 | 7 | outside |
| 12 | 14 | 13 | inside |
| 12 | 14 | 11 | outside |
| 0 | 0 | any | outside (disabled) |

## offline_ind.h — stale + offline indicator

### States

| State | Trigger | Visual |
|---|---|---|
| `ONLINE` | last success ≤ 60 s ago AND `WiFi.status() == WL_CONNECTED` | no glyph, full orange / cyan colours |
| `STALE` | last success 60..180 s ago, Wi-Fi still up | usage bars + sprite tints shift orange → darkgrey; no glyph |
| `OFFLINE` | last success > 180 s ago OR Wi-Fi not connected | pulsing pixel glyph top-right, alternating `TFT_RED` and `0x6800` (dark red) every 500 ms; colours stay darkgrey |

### Public API

```cpp
namespace offline_ind {
  enum State { ONLINE, STALE, OFFLINE };
  State state;
  void recordSuccess();
  void recordFailure();
  State update();                  // recomputes state from now() + WiFi
  uint16_t tintColor(uint16_t base);  // pass TFT_ORANGE → returns TFT_DARKGREY if stale
  void drawGlyph(TFT_eSPI& tft);   // renders if OFFLINE, otherwise clears
  bool stateChangedSinceLastDraw();
}
```

### Glyph

5×5 pixel cells (40×40 px on screen). Stored as a `static const uint8_t glyph[5][5]` literal in the header — same `7-px-in-8-px-cell` pattern as the existing banner.

Bitmap (1 = filled, 0 = blank) — a clean diagonal `X`, universally read as "broken / offline":

```
1 0 0 0 1
0 1 0 1 0
0 0 1 0 0
0 1 0 1 0
1 0 0 0 1
```

Position: `x=200, y=5`. All three current modes leave the top-right corner free, so no overlap. In SETTINGS mode the glyph is not rendered (the settings header occupies the top band).

### Stale tint

`tintColor(base)` returns `base` when state is `ONLINE`, and `TFT_DARKGREY` when `STALE` or `OFFLINE`. Callers pass the bar / sprite colour through it.

Sprite tinting: the sprite palette `{TFT_BLACK, TFT_ORANGE, TFT_BLACK, TFT_CYAN, TFT_DARKGREY, TFT_WHITE}` is replaced by a tinted copy when `state != ONLINE`: indices 1 and 3 become `TFT_DARKGREY`.

## settings_ui.h — SETTINGS mode

### Sub-mode state machine

```cpp
namespace settings_ui {
  enum Sub { BROWSE, EDIT_QH_START, EDIT_QH_END, HOLDING_RESET };
  Sub sub = BROWSE;
  unsigned long resetHoldStartMs = 0;
  bool needsFullRedraw = true;
}
```

### Layout (240 × 320)

| y-range | Content |
|---|---|
| 5..25 | "SETTINGS" pixel header (size 2, orange) |
| 28..40 | "swipe to exit" (size 1, darkgrey) |
| 50..95 | Row 0 — BRIGHTNESS |
| 95..140 | Row 1 — QUIET HOURS |
| 140..185 | Row 2 — QUIET MODE |
| 185..230 | Row 3 — AUTO-CYCLE |
| 230..275 | Row 4 — RESET |
| 280..318 | empty / padding |

Each row: label TL at `x=15`, value TR at `x=225`, an `HLine` at the row's bottom in `TFT_DARKGREY`. Row height ≈ 45 px — finger-sized targets.

### Row values

| Row | Label | Values shown |
|---|---|---|
| 0 | BRIGHTNESS | `LOW` / `MID` / `HIGH` |
| 1 | QUIET HOURS | `23:00 → 07:00` (or shows `< 23 >` / `< 07 >` while editing) |
| 2 | QUIET MODE | `OFF` / `DIM` / `SLEEP` |
| 3 | AUTO-CYCLE | `OFF` / `60s` / `30s` / `120s` |
| 4 | RESET | `hold 3s` (becomes a fill bar while holding) |

### Touch handling

Gesture classification stays in `main.cpp` (matches the existing pattern of tracking `touchStart`, `startX`, `lastX` inside the `while (ts.touched())` loop). Two new entry points dispatch to settings:

- `settings_ui::handleHoldTick(int touchY, unsigned long elapsedMs)` — called every iteration of the hold loop. Lets the RESET row paint its fill bar while the finger is still down.
- `settings_ui::handleTap(TS_Point& p, unsigned long elapsedMs)` — called after release for taps (not swipes).

While `currentMode == 3` the global 5-second factory-reset path is **suppressed**; the RESET row's 3-second hold is used instead. This avoids the two timers fighting on the same touch.

The handlers behave as follows:

1. Determine which row `y` falls into.
2. **Brief tap** (`elapsed < 500 ms`, no significant swipe):
   - Row 0: `bri = (bri + 1) % 3`; persist; `display_pm::setBrightness(bri)`; flash row.
   - Row 1: if `sub == BROWSE`, enter `EDIT_QH_START`. If already in `EDIT_QH_START` or `EDIT_QH_END`, the tap is interpreted as an up- or down-chevron — see below.
   - Row 2: `qh_m = (qh_m + 1) % 3`; persist; `display_pm::setQuietHours(qhStart, qhEnd, qh_m)`.
   - Row 3: `cyc = (cyc + 1) % 4`; persist; `display_pm::setCycle(cyc)`.
   - Row 4: ignored on brief tap — only press-and-hold triggers reset.
3. **Hold on Row 4** — while `ts.touched()` is true and finger is on row 4: `settings_ui::handleHoldTick` paints a fill bar that grows orange L→R over 3000 ms.
4. **Release before 3000 ms**: cancel reset, redraw row at its normal state.
5. **Release at / after 3000 ms**: clear NVS + WiFiManager creds, `ESP.restart()`.
6. **Swipe** is always handled by `main.cpp` regardless of mode: classified before dispatching to settings. A swipe in settings cancels any active edit / hold sub-mode and switches the global `currentMode` to the adjacent mode (0..3 wrap).

### EDIT_QH_START / EDIT_QH_END

Row 1 expands its visual to show only the hour currently being edited, with chevrons:

```
QUIET START   < 23 >
```

Top-half of the right portion (`y < rowMidY`) acts as the up-chevron — `qhStart = (qhStart + 1) % 24`. Bottom-half is the down-chevron. After a tap on the chevron the value updates and persists immediately. Tapping any other row commits the current edit and either jumps to `EDIT_QH_END` (after `START`) or back to `BROWSE` (after `END`).

A 3-second timeout in EDIT modes auto-commits and returns to `BROWSE`.

### Visual feedback

On every successful tap on a value row, the row's `bg` is flipped (orange fill, black text) for 150 ms, then redrawn normally. This gives an unambiguous "tap registered" cue on the resistive touchscreen.

## main.cpp changes

### setup()

Replace:
```cpp
pinMode(BACKLIGHT_PIN, OUTPUT); digitalWrite(BACKLIGHT_PIN, HIGH);
```

With:
```cpp
display_pm::init(prefs);
offline_ind::recordFailure();  // start in offline state until first fetch
```

The existing `prefs.begin/end` calls move into `display_pm::init()` for the new keys; the existing `url` / `tz` reads stay in `setup()`.

### loop()

1. **Touch detection:**
   - If `display_pm::isSleeping()`: any touch calls `display_pm::wake(10000)` and skips gesture classification entirely. (The screen lights to LOW; the user lifts and taps again to interact normally.)
   - Otherwise: run the existing classification loop. While `currentMode == 3`, the loop also calls `settings_ui::handleHoldTick(p.y, elapsed)` each iteration and disables the global 5-second factory-reset trigger.
   - After release: classify into swipe / tap / double-tap.
     - **Swipe**: always switches `currentMode` with `0..3` wrap. In settings, this also cancels any active edit sub-mode.
     - **Tap / double-tap**:
       - If `currentMode == 3`: `settings_ui::handleTap(p, elapsed)`.
       - Else: existing tap / double-tap logic (sprite cycle / dynamic-mode toggle).
2. `display_pm::tick();`  — always called.
3. `offline_ind::update();` — called once per iteration; cheap when state stable.
4. Auto-cycle check uses `display_pm::getCycleIntervalMs()` instead of the hard-coded `interval = 60000`. If it returns 0, auto-cycle is disabled.
5. `nextMode()` continues to do `currentMode = (currentMode + 1) % 3` — auto-cycle does not visit SETTINGS.
6. `runSprite()` / `runClock()` / `runSystem()` are modified to use `offline_ind::tintColor(...)` for orange / cyan and to call `offline_ind::drawGlyph(tft)` at the end of their render.
7. A new `runSettings()` is added that delegates to `settings_ui::render(tft, modeChanged)`.
8. The `switch(currentMode)` adds `case 3: runSettings();`.

### fetchUsage()

After the existing HTTP call:
- On HTTP 200: call `offline_ind::recordSuccess()` regardless of JSON parse outcome — the daemon is reachable, which is what the indicator tracks. (ArduinoJson already silently leaves fields at defaults on parse failure, matching existing behaviour.)
- On any non-200 status or connection failure: call `offline_ind::recordFailure()`.

## Error handling

| Failure | Behaviour |
|---|---|
| `getLocalTime()` returns false | Quiet hours treated as inactive; brightness uses configured level. |
| `ledcAttachChannel()` fails (theoretical) | Falls back to `digitalWrite(BACKLIGHT_PIN, HIGH)` permanently. Logged but not displayed. |
| NVS write fails | Silent (matches existing pattern). On next boot the row reverts to the previous saved value, or default. |
| HTTP fetchUsage fails | `offline_ind::recordFailure()` increments the failure path; after 180 s the OFFLINE state activates. |
| WiFi disconnect | `WiFi.status() != WL_CONNECTED` is checked in `offline_ind::update()` and forces OFFLINE immediately, regardless of `lastSuccessMs`. The existing WiFiManager auto-reconnect continues to run. |
| Touch on RESET row not held long enough | Fill bar cancels visually; no NVS or restart action taken. |

## Testing

No automated firmware test harness exists in this project. Tests are manual on hardware:

1. Flash firmware. Boot completes; backlight fades in from 0 to current level (smooth, not instant).
2. Swipe left through modes — confirm SPRITE → CLOCK → SYSTEM → SETTINGS cycle. Swipe right reverses.
3. Wait 60 s on SPRITE — confirm auto-cycle advances. Confirm it does NOT advance into SETTINGS (only SPRITE / CLOCK / SYSTEM rotate).
4. In SETTINGS:
   a. Tap BRIGHTNESS row → backlight changes immediately to next level. Cycle through all three.
   b. Tap QUIET HOURS row → enters edit mode with `< 23 >` style. Tap up-chevron → 24 → 00 wrap.
   c. Tap QUIET MODE row → cycles OFF / DIM / SLEEP.
   d. Tap AUTO-CYCLE row → cycles OFF / 60s / 30s / 120s. With OFF, confirm modes don't rotate.
   e. Press-and-hold RESET row 3 s → orange bar fills L→R, device restarts and re-enters captive portal.
5. Set quiet hours to `current_hour..current_hour+1`, mode = SLEEP. Within ~60 s, backlight fades to zero.
6. Tap touchscreen during sleep → backlight wakes to LOW for 10 s, then fades back to zero.
7. Set quiet hours to span midnight (e.g. `23..02`); verify behaviour at `00:30` simulated by adjusting timezone.
8. Disconnect Wi-Fi (turn off router) → within seconds, OFFLINE glyph (pixel `X`) appears top-right pulsing red. Usage bars drain to darkgrey at the 60-second stale boundary if the Wi-Fi case is somehow delayed (e.g. Wi-Fi up but daemon down).
9. Reconnect Wi-Fi → glyph disappears, full colour returns after next successful `/usage` fetch.
10. Hold full screen 5 s → existing global factory-reset still triggers (unchanged).
11. `go test -race ./...` in `daemon/` still passes (no daemon changes).

## Backwards compatibility

- Existing NVS values (`url`, `tz`) are preserved.
- New keys default to sensible values on first read.
- Captive portal flow unchanged — Wi-Fi setup and daemon URL entry still happen there.
- 5-second screen-hold factory reset preserved.
- OTA update flow preserved.
- Daemon `/usage` wire format unchanged.

## Out of scope (deferred to backlog)

- Auto-brightness via LDR (GPIO34 still available for future).
- RGB LED mood lighting (pins 4 / 16 / 17 still available).
- Buzzer alerts.
- Daemon-side web admin UI.
- Multi-host aggregation.
- Mini-games, tamagotchi, achievements.
- Push notifications via ntfy / webhooks.
- Pomodoro overlay.
