#pragma once
#include "globals.h"
#include "sprite_frames.h"
#include "offline_ind.h"
#include "capture.h"

static unsigned long lastWordChange = 0;
static uint8_t wordIdx = 0;
static const char* funWords[] = {
  "thinking...", "cogitating...", "pondering...", "vibing...",
  "brewing ideas...", "crunching...", "scheming...", "crafting...",
  "conjuring...", "noodling...", "cooking...", "manifesting...",
};

inline void runSprite() {
  if (modeChanged) { canvas->fillScreen(TFT_BLACK); spriteFrame = 0; modeChanged = false;
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
    canvas->setTextSize(2); canvas->setTextColor(TFT_ORANGE, TFT_BLACK); canvas->drawCentreString("OHMYCLAWD", 120, 5, 1);
    canvas->setTextSize(1); canvas->setTextColor(TFT_DARKGREY, TFT_BLACK); canvas->drawCentreString("v" VERSION, 120, 22, 1);
  }
  uint16_t offset = pgm_read_word(&sprite_anim_offset[spriteAnim]);
  uint8_t count = pgm_read_byte(&sprite_anim_count[spriteAnim]);
  offline_ind::drawGlyph(*canvas);
  if (animNow() - lastSpriteFrame < pgm_read_word(&sprite_hold[offset + spriteFrame])) return;
  lastSpriteFrame = animNow();
  capture::markFrame();
  uint16_t colors[6] = {
    TFT_BLACK, offline_ind::tintColor(TFT_ORANGE), TFT_BLACK,
    offline_ind::tintColor(TFT_CYAN), TFT_DARKGREY, TFT_WHITE,
  };
  const int cell = 8, px = 7;
  const int xOff = (240 - SPRITE_W * cell) / 2;
  const int yOff = (320 - SPRITE_H * cell) / 2 - 20;
  uint8_t frameBuf[SPRITE_W * SPRITE_H];
  sprite_decode_frame(offset + spriteFrame, frameBuf);
  for (int y = 0; y < SPRITE_H; y++)
    for (int x = 0; x < SPRITE_W; x++) {
      uint8_t v = frameBuf[y * SPRITE_W + x];
      canvas->fillRect(xOff + x * cell, yOff + y * cell, px, px, colors[v]);
    }
  spriteFrame = (spriteFrame + 1) % count;

  // Fun status word
  int wordY = 35;
  if (claudeWaiting == 0 && usageSession > 0) {
    if (animNow() - lastWordChange > 5000) { lastWordChange = animNow(); wordIdx = random(12); }
    canvas->fillRect(0, wordY, 240, 10, TFT_BLACK);
    canvas->setTextSize(1); canvas->setTextColor(TFT_ORANGE, TFT_BLACK);
    canvas->setTextDatum(MC_DATUM);
    canvas->drawString(funWords[wordIdx], 124, wordY + 4, 1);
    int textW = strlen(funWords[wordIdx]) * 6;
    int blockX = 124 - textW / 2 - 10;
    uint16_t blockColor = ((animNow() / 500) % 2) ? TFT_ORANGE : TFT_BLACK;
    canvas->fillRect(blockX, wordY + 1, 6, 6, blockColor);
    canvas->setTextDatum(TL_DATUM);
  } else {
    canvas->fillRect(0, wordY, 240, 10, TFT_BLACK);
    canvas->setTextSize(1); canvas->setTextColor(TFT_ORANGE, TFT_BLACK);
    canvas->setTextDatum(MC_DATUM);
    canvas->drawString("waiting...", 124, wordY + 4, 1);
    int textW = 10 * 6;
    int blockX = 124 - textW / 2 - 10;
    canvas->fillRect(blockX, wordY + 1, 6, 6, TFT_ORANGE);
    canvas->setTextDatum(TL_DATUM);
  }

  // Usage bars
  int barY = yOff + SPRITE_H * cell;
  int numCells = 20, cellW = 8, cellPx = 7;
  int barW = numCells * cellW;
  int barX = (240 - barW) / 2;
  canvas->setTextSize(1); canvas->setTextColor(TFT_ORANGE, TFT_BLACK);
  canvas->drawCentreString("SESSION", 120, barY, 1);
  int sBarY = barY + 12;
  int sFilled = (usageSession * numCells) / 100;
  uint16_t sessionColor = offline_ind::tintColor(TFT_ORANGE);
  for (int i = 0; i < numCells; i++) canvas->fillRect(barX + i * cellW, sBarY, cellPx, cellPx, (i < sFilled) ? sessionColor : TFT_DARKGREY);
  canvas->setTextColor(sessionColor, TFT_BLACK);
  canvas->drawString(String(usageSession) + "% ", barX + barW + 4, sBarY, 1);
  int sResetY = sBarY + cellPx + 3;
  canvas->setTextColor(TFT_DARKGREY, TFT_BLACK);
  String sReset = "reset ";
  if (usageSR >= 60) sReset += String(usageSR / 60) + "h";
  else sReset += String(usageSR) + "m";
  canvas->drawCentreString(sReset, 120, sResetY, 1);
  int wLabelY = sResetY + 14;
  canvas->setTextColor(TFT_CYAN, TFT_BLACK);
  canvas->drawCentreString("WEEKLY", 120, wLabelY, 1);
  int wBarY = wLabelY + 12;
  int wFilled = (usageWeekly * numCells) / 100;
  uint16_t weeklyColor = offline_ind::tintColor(TFT_CYAN);
  for (int i = 0; i < numCells; i++) canvas->fillRect(barX + i * cellW, wBarY, cellPx, cellPx, (i < wFilled) ? weeklyColor : TFT_DARKGREY);
  canvas->setTextColor(weeklyColor, TFT_BLACK);
  canvas->drawString(String(usageWeekly) + "% ", barX + barW + 4, wBarY, 1);
  int wResetY = wBarY + cellPx + 3;
  canvas->setTextColor(TFT_DARKGREY, TFT_BLACK);
  String wReset = "reset ";
  if (usageWR >= 1440) wReset += String(usageWR / 1440) + "d " + String((usageWR % 1440) / 60) + "h";
  else if (usageWR >= 60) wReset += String(usageWR / 60) + "h";
  else wReset += String(usageWR) + "m";
  canvas->drawCentreString(wReset, 120, wResetY, 1);
  offline_ind::drawGlyph(*canvas);
}
