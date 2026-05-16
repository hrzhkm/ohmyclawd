#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFiManager.h>
#include "time.h"

// --- CYD PIN CONFIGURATION ---
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33
#define BACKLIGHT_PIN 21 

const char* ssid     = "";
const char* password = "";
String LAT = "41.03"; 
String LON = "21.33";

TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

int currentMode = 0; 
bool isAutoCycle = true; 
unsigned long modeTimer = 0;
const unsigned long interval = 60000; 
bool modeChanged = true;

float cTemp = 0, cHum = 0, cWind = 0;
unsigned long lastDataFetch = 0; 
unsigned long lastCryptoFetch = 0;

// Game of Life
#define GRID_W 48
#define GRID_H 60
uint8_t grid[GRID_W][GRID_H], nextGrid[GRID_W][GRID_H];
uint16_t hueShift = 0;
int lastCellCount = 0;
int sameCountTimer = 0;

// Matrix
#define MAX_STREAMS 14
struct MatrixColumn { int x; float y; float speed; int length; char lastChar; };
MatrixColumn rain[MAX_STREAMS];

const char* ids[] = {"bitcoin", "ethereum", "binancecoin", "solana", "ripple", "cardano", "tron", "dogecoin", "shiba-inu"};
const char* symbols[] = {"BTC", "ETH", "BNB", "SOL", "XRP", "ADA", "TRX", "DGE", "SHIB"};

void nextMode();
void updateWeather();
void runCrypto();
void runWeather();
void runClock();
void runMatrix();
void runLife();
void spawnLife();

void setup() {
  tft.init();
  tft.invertDisplay(true);
  tft.setRotation(0); 
  pinMode(BACKLIGHT_PIN, OUTPUT); digitalWrite(BACKLIGHT_PIN, HIGH);
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI); ts.setRotation(0);

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(0x07FF);
  tft.drawCentreString("STARTING WIFI...", 120, 140, 2);
  tft.drawCentreString("Connect to AP:", 120, 170, 2);
  tft.setTextColor(0xFFE0);
  tft.drawCentreString("CYD-Cyberdeck", 120, 200, 4);
  tft.setTextColor(0x07FF);
  tft.drawCentreString("then open 192.168.4.1", 120, 240, 2);

  WiFiManager wm;
  wm.setConfigPortalTimeout(300);
  if (!wm.autoConnect("CYD-Cyberdeck")) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED);
    tft.drawCentreString("WIFI FAILED", 120, 150, 4);
    tft.drawCentreString("Restarting...", 120, 190, 2);
    delay(3000);
    ESP.restart();
  }
  
  tft.fillScreen(0x07E0); tft.setTextColor(TFT_BLACK);
  tft.drawCentreString("CONNECTED!", 120, 160, 4);
  configTime(3600, 3600, "pool.ntp.org");
  delay(1000); tft.fillScreen(TFT_BLACK);
  modeTimer = millis();
}

void loop() {
  if (ts.touched()) { isAutoCycle = false; nextMode(); delay(400); }
  if (isAutoCycle && (millis() - modeTimer > interval)) nextMode();

  if (millis() - lastDataFetch > 60000 || lastDataFetch == 0) {
    updateWeather(); lastDataFetch = millis();
  }

  if (currentMode == 2 && (millis() - lastCryptoFetch > 60000)) modeChanged = true; 

  switch (currentMode) {
    case 0: runClock(); break;
    case 1: runWeather(); break;
    case 2: runCrypto(); break;
    case 3: runMatrix(); break;
    case 4: runLife(); break;
  }
}

void nextMode() { currentMode = (currentMode + 1) % 5; modeChanged = true; modeTimer = millis(); tft.fillScreen(TFT_BLACK); }

void updateWeather() {
  HTTPClient http;
  http.begin("https://api.open-meteo.com/v1/forecast?latitude="+LAT+"&longitude="+LON+"&current=temperature_2m,relative_humidity_2m,wind_speed_10m&timezone=auto");
  if (http.GET() == 200) {
    JsonDocument doc; deserializeJson(doc, http.getString());
    cTemp = doc["current"]["temperature_2m"]; cHum = doc["current"]["relative_humidity_2m"]; cWind = doc["current"]["wind_speed_10m"];
  }
  http.end();
}

void runCrypto() {
  if (!modeChanged) return;
  HTTPClient http;
  http.begin("https://api.coingecko.com/api/v3/simple/price?ids=bitcoin,ethereum,binancecoin,solana,ripple,cardano,tron,dogecoin,shiba-inu&vs_currencies=usd&include_24hr_change=true");
  if (http.GET() == 200) {
    DynamicJsonDocument doc(4096); deserializeJson(doc, http.getString());
    tft.fillScreen(TFT_BLACK); tft.setTextDatum(TL_DATUM);
    tft.setTextColor(0xFFE0); tft.drawString("CRYPTO TERMINAL", 10, 5, 2);
    tft.drawFastHLine(0, 25, 240, 0x07FF);
    int yPos = 32;
    for (int i = 0; i < 9; i++) {
      float price = doc[ids[i]]["usd"]; float change = doc[ids[i]]["usd_24h_change"];
      tft.setTextColor(0xFFFF); tft.drawString(symbols[i], 5, yPos, 2);
      tft.setTextColor(0x07FF);
      String pStr;
      if (String(symbols[i]) == "SHIB") pStr = String(price, 9);
      else if (String(symbols[i]) == "DGE") pStr = String(price, 4);
      else if (String(symbols[i]) == "TRX" || String(symbols[i]) == "ADA" || String(symbols[i]) == "XRP") pStr = String(price, 4);
      else pStr = (price >= 1000) ? String((int)price) : String(price, 2);
      tft.drawString(pStr, 55, yPos, 2);
      tft.setTextColor((change >= 0) ? 0x07E0 : 0xF800);
      tft.drawRightString(String(change, 1) + "%", 235, yPos, 2);
      yPos += 31; tft.drawFastHLine(5, yPos - 3, 230, 0x2104);
    }
    lastCryptoFetch = millis(); modeChanged = false;
  }
  http.end();
}

void runWeather() {
  if (modeChanged) {
    tft.fillScreen(TFT_BLACK); tft.drawRect(0, 0, 240, 25, 0x07FF);
    tft.setTextColor(0xFFFF); tft.drawCentreString("CYBER WEATHER HUD", 120, 5, 1);
    tft.drawRoundRect(5, 30, 112, 140, 8, 0xF81F); tft.drawRoundRect(122, 30, 112, 140, 8, 0xFFE0);
    tft.drawRoundRect(5, 175, 112, 140, 8, 0x07FF); tft.drawRoundRect(122, 175, 112, 140, 8, 0x07E0);
    tft.setTextColor(0xF81F); tft.drawCentreString("TIME", 61, 35, 2);
    tft.setTextColor(0xFFE0); tft.drawCentreString("TEMP", 178, 35, 2);
    tft.setTextColor(0x07FF); tft.drawCentreString("HUMIDITY", 61, 180, 2);
    tft.setTextColor(0x07E0); tft.drawCentreString("WIND", 178, 180, 2);
    tft.setTextColor(0xFFE0); tft.drawCentreString(String(cTemp, 1)+"C", 178, 90, 4);
    tft.setTextColor(0x07FF); tft.drawCentreString(String((int)cHum)+"%", 61, 240, 4);
    tft.setTextColor(0x07E0); tft.drawCentreString(String(cWind, 1), 178, 240, 4);
    tft.setTextSize(1); tft.drawCentreString("km/h", 178, 290, 2);
    modeChanged = false;
  }
  struct tm ti; 
  if(getLocalTime(&ti)) {
    static int lsec = -1;
    if (ti.tm_sec != lsec) {
      tft.fillRect(10, 75, 102, 60, TFT_BLACK); 
      char tS[6]; strftime(tS, 6, "%H:%M", &ti);
      tft.setTextColor(0xF81F); tft.drawCentreString(tS, 61, 100, 4);
      int32_t rssi = WiFi.RSSI(); int bars = (rssi > -50) ? 4 : (rssi > -70) ? 3 : (rssi > -85) ? 2 : 1;
      for (int i = 0; i < 4; i++) { tft.fillRect(210 + (i * 6), 18 - (i * 3), 4, (i * 3) + 3, (i < bars) ? 0x07E0 : 0x3186); }
      lsec = ti.tm_sec;
    }
  }
}

void runClock() {
  if (modeChanged) {
    tft.drawRoundRect(5, 5, 230, 95, 10, 0xF81F);
    tft.drawRoundRect(5, 105, 230, 55, 10, 0x07FF);
    tft.drawRoundRect(5, 165, 230, 150, 10, 0xFFE0);
    modeChanged = false;
  }
  struct tm ti; if(!getLocalTime(&ti)) return;
  static int lsec = -1;
  if (ti.tm_sec != lsec) {
    tft.setTextDatum(MC_DATUM); tft.setTextColor(0xFFFF, TFT_BLACK);
    char tB[10]; sprintf(tB, (ti.tm_sec % 2 == 0) ? "%02d:%02d" : "%02d %02d", ti.tm_hour, ti.tm_min);
    tft.drawString(tB, 120, 50, 6); 
    char dB[20], dyB[20]; strftime(dB, 20, "%b %d, %Y", &ti); strftime(dyB, 20, "%A", &ti);
    tft.drawString(dyB, 120, 200, 4); tft.setTextColor(0xFFE0, TFT_BLACK); tft.drawString(dB, 120, 240, 4);
    if (ti.tm_sec == 0) tft.fillRect(10, 115, 220, 35, TFT_BLACK);
    for (int i = 0; i < 60; i++) {
      int xP = 10 + (i * 3.6);
      if (i <= ti.tm_sec) {
        uint8_t h = i * 4.25; uint8_t r,g,b;
        if(h<85){r=255-h*3;g=h*3;b=0;} else if(h<170){h-=85;r=0;g=255-h*3;b=h*3;} else {h-=170;r=h*3;g=0;b=255-h*3;}
        tft.fillRect(xP, 115, 2, 35, tft.color565(r,g,b));
      } else tft.fillRect(xP, 115, 2, 35, 0x2104);
    }
    int32_t rssi = WiFi.RSSI(); int bars = (rssi > -50) ? 4 : (rssi > -70) ? 3 : (rssi > -85) ? 2 : 1;
    for (int i = 0; i < 4; i++) { tft.fillRect(100 + (i * 8), 305 - ((i + 1) * 5), 6, (i + 1) * 5, (i < bars) ? 0x07E0 : 0x3186); }
    lsec = ti.tm_sec;
  }
}

void runMatrix() {
  if (modeChanged) {
    for (int i = 0; i < MAX_STREAMS; i++) {
      rain[i].x = i * 17 + 2; rain[i].y = random(-400, 0);
      rain[i].speed = random(5, 15); rain[i].length = random(15, 40);
    }
    modeChanged = false;
  }
  for (int i = 0; i < MAX_STREAMS; i++) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK); char hC = random(33, 126);
    tft.drawChar(hC, rain[i].x, (int)rain[i].y, 2);
    tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.drawChar(rain[i].lastChar, rain[i].x, (int)rain[i].y - 20, 2);
    tft.fillRect(rain[i].x, (int)rain[i].y - (rain[i].length * 20), 20, 20, TFT_BLACK);
    rain[i].lastChar = hC; rain[i].y += rain[i].speed;
    if (rain[i].y > 320 + (rain[i].length * 20)) rain[i].y = -20;
  }
  delay(25); 
}

void spawnLife() {
  tft.fillScreen(TFT_BLACK); tft.setTextColor(0x7BEF); tft.drawCentreString("GENERATING SEED...", 120, 160, 2);
  delay(800); tft.fillScreen(TFT_BLACK);
  for (int x = 0; x < GRID_W; x++) for (int y = 0; y < GRID_H; y++) grid[x][y] = (random(100) < 20) ? 1 : 0;
  sameCountTimer = 0;
}

void runLife() {
  if (modeChanged) { spawnLife(); modeChanged = false; }
  int totalAlive = 0;
  for (int x = 0; x < GRID_W; x++) {
    for (int y = 0; y < GRID_H; y++) {
      int n = 0;
      for (int i = -1; i <= 1; i++) for (int j = -1; j <= 1; j++) {
        if (i == 0 && j == 0) continue;
        if (grid[(x+i+GRID_W)%GRID_W][(y+j+GRID_H)%GRID_H] > 0) n++;
      }
      if (grid[x][y] > 0) nextGrid[x][y] = (n == 2 || n == 3) ? 1 : 0;
      else nextGrid[x][y] = (n == 3) ? 1 : 0;
      if (nextGrid[x][y] > 0) totalAlive++;
    }
  }
  for (int x = 0; x < GRID_W; x++) {
    for (int y = 0; y < GRID_H; y++) {
      if (grid[x][y] != nextGrid[x][y]) {
        if (nextGrid[x][y] > 0) {
          uint8_t r = (x * 4 + hueShift) % 255; uint8_t g = (y * 2 + hueShift / 2) % 255;
          tft.fillRect(x * 5, y * 5, 4, 4, tft.color565(r, g, 255 - r));
        } else tft.fillRect(x * 5, y * 5, 4, 4, TFT_BLACK);
      }
      grid[x][y] = nextGrid[x][y];
    }
  }
  tft.fillRect(180, 5, 55, 25, TFT_BLACK); tft.setTextColor(0x07FF); tft.drawRightString(String(totalAlive), 235, 10, 2);
  if (totalAlive == lastCellCount) sameCountTimer++; else sameCountTimer = 0;
  lastCellCount = totalAlive; hueShift += 3;
  if (totalAlive < 5 || sameCountTimer > 120) { delay(1500); spawnLife(); }
  delay(100); 
}
