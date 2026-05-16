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
int usageSession = 0;
int usageWeekly = 0;
int usageSR = 0;
int usageWR = 0;
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
void fetchUsage();
void checkOTA();

void setup() {
  tft.init();
  tft.invertDisplay(true);
  tft.setRotation(0); 
  pinMode(BACKLIGHT_PIN, OUTPUT); digitalWrite(BACKLIGHT_PIN, HIGH);
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI); ts.setRotation(0);

  // Load saved daemon URL
  prefs.begin("ohmyclawd", false);
  daemonUrl = prefs.getString("url", "http://ohmyclawd.local:8787");
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
  wm.addParameter(&daemonParam);
  wm.setConfigPortalTimeout(300);
  if (!wm.autoConnect("OhMyClawd")) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED);
    tft.setTextSize(3); tft.drawCentreString("WIFI FAILED", 120, 150, 1);
    tft.setTextSize(2); tft.drawCentreString("Restarting...", 120, 190, 1);
    delay(3000);
    ESP.restart();
  }

  // Save daemon URL if changed
  String newUrl = String(daemonParam.getValue());
  if (newUrl.length() > 0 && newUrl != daemonUrl) {
    daemonUrl = newUrl;
    prefs.begin("ohmyclawd", false);
    prefs.putString("url", daemonUrl);
    prefs.end();
  }
  
  tft.fillScreen(TFT_ORANGE); tft.setTextColor(TFT_BLACK);
  tft.setTextSize(3); tft.drawCentreString("CONNECTED!", 120, 160, 1);
  configTime(3600, 3600, "pool.ntp.org");
  delay(1000);
  checkOTA();
  tft.fillScreen(TFT_BLACK);
  modeTimer = millis();
}

void loop() {
  if (ts.touched()) {
    unsigned long touchStart = millis();
    while (ts.touched() && (millis() - touchStart < 5000)) delay(50);
    if (millis() - touchStart >= 5000) {
      // 5s hold: reset WiFi and reboot
      WiFiManager wm;
      wm.resetSettings();
      prefs.begin("ohmyclawd", false); prefs.clear(); prefs.end();
      ESP.restart();
    }
    isAutoCycle = false; nextMode(); delay(400);
  }
  if (isAutoCycle && (millis() - modeTimer > interval)) nextMode();
  if (millis() - lastUsageFetch > 30000 || lastUsageFetch == 0) { fetchUsage(); lastUsageFetch = millis(); }

  switch (currentMode) {
    case 0: runSprite(); break;
    case 1: runClock(); break;
  }
}

void nextMode() { currentMode = (currentMode + 1) % 2; modeChanged = true; modeTimer = millis(); tft.fillScreen(TFT_BLACK); }

void runSprite() {
  if (modeChanged) { tft.fillScreen(TFT_BLACK); spriteFrame = 0; modeChanged = false;
    // Pick sprite based on status: limited=sleep(2), high=work-think(8), mid=work-coding(12), low=random dance
    if (usageSession >= 80) spriteAnim = 2;       // expression-sleep (rate limited territory)
    else if (usageSession >= 50) spriteAnim = 8;  // work-think
    else if (usageSession >= 25) spriteAnim = 12; // work-coding
    else spriteAnim = random(2);                  // dance-bounce(0) or dance-sway(1)
    tft.setTextSize(2); tft.setTextColor(TFT_ORANGE, TFT_BLACK); tft.drawCentreString("OHMYCLAWD", 120, 5, 1);
    tft.setTextSize(1); tft.setTextColor(TFT_DARKGREY, TFT_BLACK); tft.drawCentreString("v" VERSION, 120, 22, 1);
  }
  uint16_t offset = pgm_read_word(&sprite_anim_offset[spriteAnim]);
  uint8_t count = pgm_read_byte(&sprite_anim_count[spriteAnim]);
  if (millis() - lastSpriteFrame < pgm_read_word(&sprite_hold[offset + spriteFrame])) return;
  lastSpriteFrame = millis();
  static const uint16_t colors[] = {TFT_BLACK, TFT_ORANGE, TFT_BLACK, TFT_CYAN, TFT_DARKGREY, TFT_WHITE};
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
  for (int i = 0; i < numCells; i++) tft.fillRect(barX + i * cellW, sBarY, cellPx, cellPx, (i < sFilled) ? TFT_ORANGE : TFT_DARKGREY);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
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
  for (int i = 0; i < numCells; i++) tft.fillRect(barX + i * cellW, wBarY, cellPx, cellPx, (i < wFilled) ? TFT_CYAN : TFT_DARKGREY);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(String(usageWeekly) + "% ", barX + barW + 4, wBarY, 1);
  int wResetY = wBarY + cellPx + 3;
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  String wReset = "reset ";
  if (usageWR >= 1440) wReset += String(usageWR / 1440) + "d " + String((usageWR % 1440) / 60) + "h";
  else if (usageWR >= 60) wReset += String(usageWR / 60) + "h";
  else wReset += String(usageWR) + "m";
  tft.drawCentreString(wReset, 120, wResetY, 1);
}

void fetchUsage() {
  HTTPClient http;
  http.begin(daemonUrl + "/usage");
  if (http.GET() == 200) {
    JsonDocument doc; deserializeJson(doc, http.getString());
    usageSession = doc["s"] | 0;
    usageWeekly = doc["w"] | 0;
    usageSR = doc["sr"] | 0;
    usageWR = doc["wr"] | 0;
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
  }
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
  tft.drawCentreString("UPDATE", 120, 40, 1);
  tft.setTextSize(1); tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawCentreString("v" + current + " -> v" + tag, 120, 70, 1);
  // YES button
  tft.fillRect(30, 120, 80, 50, TFT_ORANGE);
  tft.setTextSize(2); tft.setTextColor(TFT_BLACK);
  tft.drawCentreString("YES", 70, 135, 1);
  // NO button
  tft.fillRect(130, 120, 80, 50, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE);
  tft.drawCentreString("NO", 170, 135, 1);

  // Wait for touch
  unsigned long timeout = millis() + 15000;
  bool accepted = false;
  while (millis() < timeout) {
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      // Map touch coordinates (XPT2046 raw: ~300-3900)
      int tx = map(p.x, 300, 3900, 0, 240);
      int ty = map(p.y, 300, 3900, 0, 320);
      if (ty >= 120 && ty <= 170) {
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
