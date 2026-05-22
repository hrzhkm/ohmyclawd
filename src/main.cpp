#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include "time.h"
#include "sprite_frames.h"
#include "display_pm.h"
#include "offline_ind.h"
#include "settings_ui.h"

// --- CYD PIN CONFIGURATION ---
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33
#define BACKLIGHT_PIN 21 

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
Preferences prefs;

int currentMode = 0; 
bool isAutoCycle = true; 
unsigned long modeTimer = 0;
const unsigned long interval = 60000; 
bool modeChanged = true;

uint8_t spriteFrame = 0;
uint8_t spriteAnim = 0;
unsigned long lastSpriteFrame = 0;
unsigned long lastTapTime = 0;
bool dynamicSprite = true;
unsigned long lastWordChange = 0;
uint8_t wordIdx = 0;
const char* funWords[] = {
  "thinking...", "cogitating...", "pondering...", "vibing...",
  "brewing ideas...", "crunching...", "scheming...", "crafting...",
  "conjuring...", "noodling...", "cooking...", "manifesting...",
};
int usageSession = 0;
int usageWeekly = 0;
int usageSR = 0;
int usageWR = 0;
int claudeWaiting = 0;
unsigned long lastUsageFetch = 0;

String daemonUrl;

// Pixel banner "OH MY" / "CLAWD" (19x5 each)
#define BANNER_W 19
#define BANNER_H 5
static const uint8_t banner_ohmy[BANNER_H][BANNER_W] PROGMEM = {
  {1,1,1,0,1,0,1,0,0,0,0,0,1,0,1,0,1,0,1},
  {1,0,1,0,1,0,1,0,0,0,0,0,1,1,1,0,1,0,1},
  {1,0,1,0,1,1,1,0,0,0,0,0,1,1,1,0,0,1,0},
  {1,0,1,0,1,0,1,0,0,0,0,0,1,0,1,0,0,1,0},
  {1,1,1,0,1,0,1,0,0,0,0,0,1,0,1,0,0,1,0},
};
static const uint8_t banner_clawd[BANNER_H][BANNER_W] PROGMEM = {
  {1,1,1,0,1,0,0,0,1,1,1,0,1,0,1,0,1,1,0},
  {1,0,0,0,1,0,0,0,1,0,1,0,1,0,1,0,1,0,1},
  {1,0,0,0,1,0,0,0,1,1,1,0,1,1,1,0,1,0,1},
  {1,0,0,0,1,0,0,0,1,0,1,0,1,1,1,0,1,0,1},
  {1,1,1,0,1,1,1,0,1,0,1,0,1,0,1,0,1,1,0},
};

void nextMode();
void runSprite();
void runClock();
void runSystem();
void fetchUsage();
void checkOTA();
void runSettings();

void setup() {
  tft.init();
  tft.invertDisplay(true);
  tft.setRotation(0);
  display_pm::init(prefs);
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI); ts.setRotation(0);

  // Load saved settings
  prefs.begin("ohmyclawd", false);
  daemonUrl = prefs.getString("url", "http://ohmyclawd.local:8787");
  String tzStr = prefs.getString("tz", "UTC-8");
  prefs.end();

  tft.fillScreen(TFT_BLACK);
  // Draw pixel banner on boot
  const int cell = 8, px = 7;
  int xOff = (240 - BANNER_W * cell) / 2;
  int yOff = 30;
  for (int r = 0; r < BANNER_H; r++)
    for (int c = 0; c < BANNER_W; c++)
      if (pgm_read_byte(&banner_ohmy[r][c])) tft.fillRect(xOff + c * cell, yOff + r * cell, px, px, TFT_ORANGE);
  yOff += BANNER_H * cell + 4;
  for (int r = 0; r < BANNER_H; r++)
    for (int c = 0; c < BANNER_W; c++)
      if (pgm_read_byte(&banner_clawd[r][c])) tft.fillRect(xOff + c * cell, yOff + r * cell, px, px, TFT_ORANGE);

  tft.setTextColor(TFT_DARKGREY);
  tft.setTextSize(1); tft.drawCentreString("Connect to AP:", 120, 160, 1);
  tft.setTextColor(TFT_ORANGE);
  tft.setTextSize(2); tft.drawCentreString("OhMyClawd", 120, 180, 1);
  tft.setTextColor(TFT_DARKGREY);
  tft.setTextSize(1); tft.drawCentreString("then open 192.168.4.1", 120, 210, 1);

  WiFiManager wm;
  WiFiManagerParameter daemonParam("daemon_url", "Daemon URL", daemonUrl.c_str(), 80);
  WiFiManagerParameter tzParam("timezone", "Timezone (POSIX)", tzStr.c_str(), 40);
  wm.addParameter(&daemonParam);
  wm.addParameter(&tzParam);
  wm.setConfigPortalTimeout(300);
  if (!wm.autoConnect("OhMyClawd")) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED);
    tft.setTextSize(3); tft.drawCentreString("WIFI FAILED", 120, 150, 1);
    tft.setTextSize(2); tft.drawCentreString("Restarting...", 120, 190, 1);
    delay(3000);
    ESP.restart();
  }

  // Save settings if changed
  String newUrl = String(daemonParam.getValue());
  String newTz = String(tzParam.getValue());
  if ((newUrl.length() > 0 && newUrl != daemonUrl) || (newTz.length() > 0 && newTz != tzStr)) {
    if (newUrl.length() > 0) daemonUrl = newUrl;
    if (newTz.length() > 0) tzStr = newTz;
    prefs.begin("ohmyclawd", false);
    prefs.putString("url", daemonUrl);
    prefs.putString("tz", tzStr);
    prefs.end();
  }
  
  tft.fillScreen(TFT_ORANGE); tft.setTextColor(TFT_BLACK);
  tft.setTextSize(3); tft.drawCentreString("CONNECTED!", 120, 160, 1);
  configTime(0, 0, "pool.ntp.org");
  setenv("TZ", tzStr.c_str(), 1);
  tzset();
  delay(1000);
  checkOTA();
  tft.fillScreen(TFT_BLACK);
  modeTimer = millis();
}

void loop() {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    int startX = map(p.x, 300, 3900, 0, 240);
    int lastX = startX;
    unsigned long touchStart = millis();
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
    unsigned long elapsed = millis() - touchStart;
    int deltaX = lastX - startX;
    if (currentMode == 3) settings_ui::cancelHold();
    if (elapsed < 500 && abs(deltaX) > 40) {
      // Swipe
      isAutoCycle = false;
      if (currentMode == 3) settings_ui::exit();
      if (deltaX > 0) { currentMode = (currentMode + 3) % 4; } // swipe right = prev (of 4)
      else { currentMode = (currentMode + 1) % 4; }            // swipe left  = next (of 4)
      modeChanged = true; modeTimer = millis(); tft.fillScreen(TFT_BLACK);
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
    delay(200);
  }
  display_pm::tick();
  offline_ind::update();
  if (isAutoCycle && (millis() - modeTimer > interval)) nextMode();
  if (millis() - lastUsageFetch > 30000 || lastUsageFetch == 0) { fetchUsage(); lastUsageFetch = millis(); }

  switch (currentMode) {
    case 0: runSprite(); break;
    case 1: runClock(); break;
    case 2: runSystem(); break;
    case 3: runSettings(); break;
  }
}

void runSettings() {
  if (modeChanged) {
    settings_ui::enter();
    modeChanged = false;
    settings_ui::render(tft, true);
    offline_ind::drawGlyph(tft);
    return;
  }
  settings_ui::render(tft, false);
  offline_ind::drawGlyph(tft);
}

void nextMode() { currentMode = (currentMode + 1) % 3; modeChanged = true; modeTimer = millis(); tft.fillScreen(TFT_BLACK); }

void runSprite() {
  if (modeChanged) { tft.fillScreen(TFT_BLACK); spriteFrame = 0; modeChanged = false;
    // Pick sprite based on status with variety
    // Waiting: surprise(3), wink(4)
    // Rate limited (>=80%): sleep(2), idle-breathe(6)
    // Heavy (50-79%): work-think(8), idle-look-around(7)
    // Moderate (25-49%): work-coding(12), dance-djmix(11)
    // Light (<25%): dance-bounce(0), dance-sway(1), dance-bounce-dj(9), dance-sway-dj(10)
    static const uint8_t waitPool[] = {3, 4};
    static const uint8_t limitPool[] = {2, 6};
    static const uint8_t heavyPool[] = {8, 7};
    static const uint8_t modPool[] = {12, 11};
    static const uint8_t lightPool[] = {0, 1, 9, 10, 5};
    if (claudeWaiting > 0) spriteAnim = waitPool[random(2)];
    else if (usageSession >= 80) spriteAnim = limitPool[random(2)];
    else if (usageSession >= 50) spriteAnim = heavyPool[random(2)];
    else if (usageSession >= 25) spriteAnim = modPool[random(2)];
    else spriteAnim = lightPool[random(5)];
    tft.setTextSize(2); tft.setTextColor(TFT_ORANGE, TFT_BLACK); tft.drawCentreString("OHMYCLAWD", 120, 5, 1);
    tft.setTextSize(1); tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.drawCentreString("v" VERSION, 120, 22, 1);
  }
  uint16_t offset = pgm_read_word(&sprite_anim_offset[spriteAnim]);
  uint8_t count = pgm_read_byte(&sprite_anim_count[spriteAnim]);
  offline_ind::drawGlyph(tft);
  if (millis() - lastSpriteFrame < pgm_read_word(&sprite_hold[offset + spriteFrame])) return;
  lastSpriteFrame = millis();
  uint16_t colors[6] = {
    TFT_BLACK,
    offline_ind::tintColor(TFT_ORANGE),
    TFT_BLACK,
    offline_ind::tintColor(TFT_CYAN),
    TFT_DARKGREY,
    TFT_WHITE,
  };
  const int cell = 8;
  const int px = 7;
  const int xOff = (240 - SPRITE_W * cell) / 2;
  const int yOff = (320 - SPRITE_H * cell) / 2 - 20;
  for (int y = 0; y < SPRITE_H; y++) {
    for (int x = 0; x < SPRITE_W; x++) {
      uint8_t v = pgm_read_byte(&sprite_data[offset + spriteFrame][y * SPRITE_W + x]);
      tft.fillRect(xOff + x * cell, yOff + y * cell, px, px, colors[v]);
    }
  }
  spriteFrame = (spriteFrame + 1) % count;
  // Fun status word above sprite with pulsing block
  if (millis() - lastWordChange > 5000) {
    lastWordChange = millis();
    wordIdx = random(12);
  }
  // Fun status word with pulsing block (only when active)
  int wordY = 35;
  if (claudeWaiting == 0 && usageSession > 0) {
    if (millis() - lastWordChange > 5000) {
      lastWordChange = millis();
      wordIdx = random(12);
    }
    tft.fillRect(0, wordY, 240, 10, TFT_BLACK);
    tft.setTextSize(1); tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(funWords[wordIdx], 124, wordY + 4, 1);
    int textW = strlen(funWords[wordIdx]) * 6;
    int blockX = 124 - textW / 2 - 10;
    uint16_t blockColor = ((millis() / 500) % 2) ? TFT_ORANGE : TFT_BLACK;
    tft.fillRect(blockX, wordY + 1, 6, 6, blockColor);
    tft.setTextDatum(TL_DATUM);
  } else {
    tft.fillRect(0, wordY, 240, 10, TFT_BLACK);
    tft.setTextSize(1); tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("waiting...", 124, wordY + 4, 1);
    int textW = 10 * 6;
    int blockX = 124 - textW / 2 - 10;
    tft.fillRect(blockX, wordY + 1, 6, 6, TFT_ORANGE);
    tft.setTextDatum(TL_DATUM);
  }
  // Usage bars
  int barY = yOff + SPRITE_H * cell + 10;
  int numCells = 20;
  int cellW = 8;
  int cellPx = 7;
  int barW = numCells * cellW;
  int barX = (240 - barW) / 2;
  tft.setTextSize(1); tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawCentreString("SESSION", 120, barY, 1);
  int sBarY = barY + 12;
  int sFilled = (usageSession * numCells) / 100;
  uint16_t sessionColor = offline_ind::tintColor(TFT_ORANGE);
  for (int i = 0; i < numCells; i++) tft.fillRect(barX + i * cellW, sBarY, cellPx, cellPx, (i < sFilled) ? sessionColor : TFT_DARKGREY);
  tft.setTextColor(sessionColor, TFT_BLACK);
  tft.drawString(String(usageSession) + "% ", barX + barW + 4, sBarY, 1);
  int sResetY = sBarY + cellPx + 3;
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  String sReset = "reset ";
  if (usageSR >= 60) sReset += String(usageSR / 60) + "h";
  else sReset += String(usageSR) + "m";
  tft.drawCentreString(sReset, 120, sResetY, 1);
  int wLabelY = sResetY + 14;
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawCentreString("WEEKLY", 120, wLabelY, 1);
  int wBarY = wLabelY + 12;
  int wFilled = (usageWeekly * numCells) / 100;
  uint16_t weeklyColor = offline_ind::tintColor(TFT_CYAN);
  for (int i = 0; i < numCells; i++) tft.fillRect(barX + i * cellW, wBarY, cellPx, cellPx, (i < wFilled) ? weeklyColor : TFT_DARKGREY);
  tft.setTextColor(weeklyColor, TFT_BLACK);
  tft.drawString(String(usageWeekly) + "% ", barX + barW + 4, wBarY, 1);
  int wResetY = wBarY + cellPx + 3;
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  String wReset = "reset ";
  if (usageWR >= 1440) wReset += String(usageWR / 1440) + "d " + String((usageWR % 1440) / 60) + "h";
  else if (usageWR >= 60) wReset += String(usageWR / 60) + "h";
  else wReset += String(usageWR) + "m";
  tft.drawCentreString(wReset, 120, wResetY, 1);
  offline_ind::drawGlyph(tft);
}

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

void runClock() {
  if (modeChanged) {
    tft.fillScreen(TFT_BLACK);
    // Draw pixel banner
    const int cell = 8, px = 7;
    int xOff = (240 - BANNER_W * cell) / 2;
    int yOff = 10;
    for (int r = 0; r < BANNER_H; r++)
      for (int c = 0; c < BANNER_W; c++)
        if (pgm_read_byte(&banner_ohmy[r][c])) tft.fillRect(xOff + c * cell, yOff + r * cell, px, px, TFT_ORANGE);
    yOff += BANNER_H * cell + 4;
    for (int r = 0; r < BANNER_H; r++)
      for (int c = 0; c < BANNER_W; c++)
        if (pgm_read_byte(&banner_clawd[r][c])) tft.fillRect(xOff + c * cell, yOff + r * cell, px, px, TFT_ORANGE);
    modeChanged = false;
  }
  struct tm ti; if(!getLocalTime(&ti)) return;
  static int lsec = -1;
  if (ti.tm_sec != lsec) {
    tft.setTextDatum(MC_DATUM); tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    char tB[10]; sprintf(tB, (ti.tm_sec % 2 == 0) ? "%02d:%02d" : "%02d %02d", ti.tm_hour, ti.tm_min);
    tft.setTextSize(4); tft.drawString(tB, 120, 140, 1);
    char dB[20], dyB[20]; strftime(dB, 20, "%b %d, %Y", &ti); strftime(dyB, 20, "%A", &ti);
    tft.setTextSize(2); tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString(dyB, 120, 190, 1);
    tft.drawString(dB, 120, 220, 1);
    // Second progress bar (grid cells)
    int barX = (240 - BANNER_W * 8) / 2;
    int numCells = BANNER_W;
    int barY = 260;
    int filled = (ti.tm_sec * numCells) / 60;
    for (int i = 0; i < numCells; i++)
      tft.fillRect(barX + i * 8, barY, 7, 7, (i <= filled) ? TFT_ORANGE : TFT_DARKGREY);
    lsec = ti.tm_sec;
    offline_ind::drawGlyph(tft);
  }
  offline_ind::drawGlyph(tft);
}

void runSystem() {
  offline_ind::drawGlyph(tft);
  if (!modeChanged) return;
  modeChanged = false;
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2); tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawCentreString("OHMYCLAWD", 120, 5, 1);
  tft.setTextSize(1); tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawCentreString("SYSTEM", 120, 22, 1);

  int y = 45;
  int labelX = 15, valX = 225, gap = 20;

  auto drawRow = [&](const char* label, String value) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(label, labelX, y, 1);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(value, valX, y, 1);
    y += 10;
    tft.drawFastHLine(labelX, y, valX - labelX, TFT_DARKGREY);
    y += gap - 10;
  };

  drawRow("CHIP", String(ESP.getChipModel()) + " Rev" + String(ESP.getChipRevision()) + " " + String(ESP.getChipCores()) + "c");
  drawRow("CPU", String(ESP.getCpuFreqMHz()) + "MHz");
  drawRow("SDK", ESP.getSdkVersion());
  drawRow("FLASH", String(ESP.getFlashChipSize() / 1024 / 1024) + "MB @ " + String(ESP.getFlashChipSpeed() / 1000000) + "MHz");
  drawRow("HEAP", String(ESP.getFreeHeap() / 1024) + "KB / " + String(ESP.getHeapSize() / 1024) + "KB");
  drawRow("PANEL", "ILI9341 240x320");
  drawRow("TOUCH", "XPT2046");

  // WiFi row with grid bar
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("WIFI", labelX, y, 1);
  int32_t rssi = WiFi.RSSI();
  int bars = (rssi > -50) ? 5 : (rssi > -60) ? 4 : (rssi > -70) ? 3 : (rssi > -80) ? 2 : 1;
  for (int i = 0; i < 5; i++)
    tft.fillRect(130 + i * 8, y, 6, 6, (i < bars) ? TFT_ORANGE : TFT_DARKGREY);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(String(rssi) + "dBm", valX, y, 1);
  y += 10; tft.drawFastHLine(labelX, y, valX - labelX, TFT_DARKGREY); y += gap - 10;

  drawRow("IP", WiFi.localIP().toString());

  // Uptime
  unsigned long sec = millis() / 1000;
  int d = sec / 86400, h = (sec % 86400) / 3600, m = (sec % 3600) / 60;
  String up = "";
  if (d > 0) up += String(d) + "d ";
  up += String(h) + "h " + String(m) + "m";
  drawRow("UP", up);

  drawRow("FW", "v" VERSION);
  drawRow("BY", "opariffazman");
  drawRow("GH", "opariffazman/ohmyclawd");
  tft.setTextDatum(TL_DATUM);
}

void checkOTA() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(1); tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawCentreString("checking for updates...", 120, 155, 1);

  WiFiClientSecure client;
  client.setInsecure(); // GitHub API, skip cert verification
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(client, "https://api.github.com/repos/opariffazman/ohmyclawd/releases/latest");
  http.addHeader("User-Agent", "OhMyClawd-ESP32");
  int code = http.GET();
  if (code != 200) { http.end(); return; }

  JsonDocument doc;
  deserializeJson(doc, http.getString());
  http.end();

  String tag = doc["tag_name"] | "";
  if (tag.startsWith("v")) tag = tag.substring(1);
  String current = VERSION;
  if (tag.length() == 0 || tag == current) return;

  // Find firmware asset URL
  String assetUrl = "";
  for (JsonObject asset : doc["assets"].as<JsonArray>()) {
    String name = asset["name"] | "";
    if (name == "ohmyclawd-firmware.bin") {
      assetUrl = asset["browser_download_url"].as<String>();
      break;
    }
  }
  if (assetUrl.length() == 0) return;

  // Show update prompt
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2); tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawCentreString("UPDATE", 120, 20, 1);
  tft.setTextSize(1); tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawCentreString("v" + current + " -> v" + tag, 120, 45, 1);
  // YES button
  tft.fillRect(30, 70, 80, 40, TFT_ORANGE);
  tft.setTextSize(2); tft.setTextColor(TFT_BLACK);
  tft.drawCentreString("YES", 70, 80, 1);
  // NO button
  tft.fillRect(130, 70, 80, 40, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE);
  tft.drawCentreString("NO", 170, 80, 1);
  // Release notes
  String notes = doc["body"] | "";
  if (notes.length() > 0) {
    tft.setTextSize(1); tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawCentreString("What's new:", 120, 125, 1);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    int noteY = 140;
    int lineStart = 0;
    int linesShown = 0;
    for (int i = 0; i <= (int)notes.length() && linesShown < 6; i++) {
      if (i == (int)notes.length() || notes[i] == '\n') {
        String line = notes.substring(lineStart, i);
        line.trim();
        if (line.length() > 0) {
          if (line.length() > 38) line = line.substring(0, 38) + "..";
          tft.drawString(line, 5, noteY, 1);
          noteY += 12;
          linesShown++;
        }
        lineStart = i + 1;
      }
    }
  }

  // Wait for touch
  unsigned long timeout = millis() + 15000;
  bool accepted = false;
  while (millis() < timeout) {
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      // Map touch coordinates (XPT2046 raw: ~300-3900)
      int tx = map(p.x, 300, 3900, 0, 240);
      int ty = map(p.y, 300, 3900, 0, 320);
      if (ty >= 70 && ty <= 110) {
        if (tx >= 30 && tx <= 110) { accepted = true; break; }
        if (tx >= 130 && tx <= 210) { return; }
      }
      delay(200);
    }
    delay(50);
  }
  if (!accepted) return;

  // Download and flash
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2); tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawCentreString("UPDATING", 120, 5, 1);
  tft.setTextSize(1); tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawCentreString("do not power off", 120, 25, 1);

  // Draw sleep sprite centered
  const int cell = 8, px = 7;
  const int xOff = (240 - SPRITE_W * cell) / 2;
  const int yOff = 45;
  uint16_t sleepOffset = pgm_read_word(&sprite_anim_offset[2]); // expression-sleep
  static const uint16_t colors[] = {TFT_BLACK, TFT_ORANGE, TFT_BLACK, TFT_CYAN, TFT_DARKGREY, TFT_WHITE};
  for (int y = 0; y < SPRITE_H; y++)
    for (int x = 0; x < SPRITE_W; x++) {
      uint8_t v = pgm_read_byte(&sprite_data[sleepOffset][y * SPRITE_W + x]);
      tft.fillRect(xOff + x * cell, yOff + y * cell, px, px, colors[v]);
    }

  // Progress bar params
  int barX = (240 - BANNER_W * 8) / 2;
  int barY = yOff + SPRITE_H * cell + 15;
  int numCells = BANNER_W;

  http.begin(client, assetUrl);
  http.addHeader("User-Agent", "OhMyClawd-ESP32");
  code = http.GET();
  if (code != 200) { http.end(); return; }

  int contentLen = http.getSize();
  WiFiClient* stream = http.getStreamPtr();

  if (!Update.begin(contentLen)) { http.end(); return; }

  uint8_t buf[1024];
  int written = 0;
  uint8_t sleepFrame = 0;
  uint8_t sleepCount = pgm_read_byte(&sprite_anim_count[2]);
  unsigned long lastFrame = 0;
  while (written < contentLen) {
    int avail = stream->available();
    if (avail) {
      int read = stream->readBytes(buf, min((int)sizeof(buf), avail));
      Update.write(buf, read);
      written += read;
      // Update progress bar
      int pct = (written * 100) / contentLen;
      int filled = (pct * numCells) / 100;
      for (int i = 0; i < numCells; i++)
        tft.fillRect(barX + i * 8, barY, 7, 7, (i <= filled) ? TFT_ORANGE : TFT_DARKGREY);
      // Percentage text
      tft.fillRect(barX + numCells * 8 + 4, barY, 30, 8, TFT_BLACK);
      tft.setTextSize(1); tft.setTextColor(TFT_ORANGE, TFT_BLACK);
      tft.drawString(String(pct) + "%", barX + numCells * 8 + 4, barY, 1);
    }
    // Animate sleep sprite
    if (millis() - lastFrame > 150) {
      lastFrame = millis();
      uint16_t fOff = sleepOffset + sleepFrame;
      for (int y = 0; y < SPRITE_H; y++)
        for (int x = 0; x < SPRITE_W; x++) {
          uint8_t v = pgm_read_byte(&sprite_data[fOff][y * SPRITE_W + x]);
          tft.fillRect(xOff + x * cell, yOff + y * cell, px, px, colors[v]);
        }
      sleepFrame = (sleepFrame + 1) % sleepCount;
    }
    delay(1);
  }

  http.end();
  if (Update.end(true)) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2); tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.drawCentreString("DONE!", 120, 150, 1);
    tft.setTextSize(1); tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawCentreString("rebooting...", 120, 180, 1);
    delay(2000);
    ESP.restart();
  }
}
