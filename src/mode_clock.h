#pragma once
#include "globals.h"
#include "offline_ind.h"
#include "time.h"

inline void runClock() {
  if (modeChanged) {
    tft.fillScreen(TFT_BLACK);
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
