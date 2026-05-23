#pragma once
#include "globals.h"
#include "offline_ind.h"
#include <WiFi.h>

inline void runSystem() {
  offline_ind::drawGlyph(tft);
  if (!modeChanged) return;
  modeChanged = false;
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2); tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("SYSTEM", 120, 15, 1);
  tft.setTextSize(1);

  int y = 40;
  int labelX = 5, valX = 235, gap = 18;

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

  // WiFi row with signal bars
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("WIFI", labelX, y, 1);
  int32_t rssi = WiFi.RSSI();
  int bars = (rssi > -50) ? 5 : (rssi > -60) ? 4 : (rssi > -70) ? 3 : (rssi > -80) ? 2 : 1;
  for (int i = 0; i < 5; i++)
    tft.fillRect(120 + i * 8, y, 6, 6, (i < bars) ? TFT_ORANGE : TFT_DARKGREY);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(String(rssi) + "dBm", valX, y, 1);
  y += 10; tft.drawFastHLine(labelX, y, valX - labelX, TFT_DARKGREY); y += gap - 10;

  drawRow("IP", WiFi.localIP().toString());

  unsigned long sec = millis() / 1000;
  int d = sec / 86400, h = (sec % 86400) / 3600, m = (sec % 3600) / 60;
  String up = "";
  if (d > 0) up += String(d) + "d ";
  up += String(h) + "h " + String(m) + "m";
  drawRow("UP", up);

  drawRow("FW", "v" VERSION);
  drawRow("GH", "opariffazman/ohmyclawd");
  tft.setTextDatum(TL_DATUM);
}
