#pragma once
#include "globals.h"
#include "offline_ind.h"
#include "capture.h"
#include <WiFi.h>

inline void runSystem() {
  offline_ind::drawGlyph(*canvas);
  if (!modeChanged) { capture::markFrame(); return; }
  modeChanged = false;
  canvas->fillScreen(TFT_BLACK);
  canvas->setTextSize(2); canvas->setTextColor(TFT_ORANGE, TFT_BLACK);
  canvas->setTextDatum(MC_DATUM);
  canvas->drawString("SYSTEM", 120, 15, 1);
  canvas->setTextSize(1);

  int y = 40;
  int labelX = 5, valX = 235, gap = 18;

  auto drawRow = [&](const char* label, String value) {
    canvas->setTextColor(TFT_DARKGREY, TFT_BLACK);
    canvas->setTextDatum(TL_DATUM);
    canvas->drawString(label, labelX, y, 1);
    canvas->setTextColor(TFT_ORANGE, TFT_BLACK);
    canvas->setTextDatum(TR_DATUM);
    canvas->drawString(value, valX, y, 1);
    y += 10;
    canvas->drawFastHLine(labelX, y, valX - labelX, TFT_DARKGREY);
    y += gap - 10;
  };

  drawRow("CHIP", String(ESP.getChipModel()) + " Rev" + String(ESP.getChipRevision()) + " " + String(ESP.getChipCores()) + "c");
  drawRow("CPU", String(ESP.getCpuFreqMHz()) + "MHz");
  drawRow("SDK", ESP.getSdkVersion());
  drawRow("FLASH", String(ESP.getFlashChipSize() / 1024 / 1024) + "MB @ " + String(ESP.getFlashChipSpeed() / 1000000) + "MHz");
  drawRow("HEAP", String(ESP.getFreeHeap() / 1024) + "KB / " + String(ESP.getHeapSize() / 1024) + "KB");
  drawRow("PANEL", "ILI9341 240x320");
  drawRow("TOUCH", "XPT2046");

  canvas->setTextColor(TFT_DARKGREY, TFT_BLACK);
  canvas->setTextDatum(TL_DATUM);
  canvas->drawString("WIFI", labelX, y, 1);
  int32_t rssi = WiFi.RSSI();
  int bars = (rssi > -50) ? 5 : (rssi > -60) ? 4 : (rssi > -70) ? 3 : (rssi > -80) ? 2 : 1;
  for (int i = 0; i < 5; i++)
    canvas->fillRect(120 + i * 8, y, 6, 6, (i < bars) ? TFT_ORANGE : TFT_DARKGREY);
  canvas->setTextColor(TFT_ORANGE, TFT_BLACK);
  canvas->setTextDatum(TR_DATUM);
  canvas->drawString(String(rssi) + "dBm", valX, y, 1);
  y += 10; canvas->drawFastHLine(labelX, y, valX - labelX, TFT_DARKGREY); y += gap - 10;

  drawRow("IP", WiFi.localIP().toString());

  unsigned long sec = millis() / 1000;
  int d = sec / 86400, h = (sec % 86400) / 3600, m = (sec % 3600) / 60;
  String up = "";
  if (d > 0) up += String(d) + "d ";
  up += String(h) + "h " + String(m) + "m";
  drawRow("UP", up);

  drawRow("FW", "v" VERSION);
  drawRow("GH", "opariffazman/ohmyclawd");
  canvas->setTextDatum(TL_DATUM);
  capture::markFrame();
}
