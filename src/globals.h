#pragma once
#include <TFT_eSPI.h>
#include <Preferences.h>

extern TFT_eSPI tft;
extern Preferences prefs;
extern int currentMode;
extern unsigned long modeTimer;
extern bool modeChanged;
extern int usageSession, usageWeekly, usageSR, usageWR, claudeWaiting;
extern String daemonUrl;

// Sprite state (shared between main loop tap handling and mode_sprite)
extern uint8_t spriteFrame;
extern uint8_t spriteAnim;
extern unsigned long lastSpriteFrame;
extern bool dynamicSprite;

// Navigation
const int NAV_Y = 295, NAV_H = 14;
const int NAV_LEFT_X = 80, NAV_RIGHT_X = 140, NAV_BTN_W = 20;
void drawPageIndicator();
bool handleNavTap(int tapX, int tapY);

// Banner
#define BANNER_W 19
#define BANNER_H 5
extern const uint8_t banner_ohmy[BANNER_H][BANNER_W] PROGMEM;
extern const uint8_t banner_clawd[BANNER_H][BANNER_W] PROGMEM;
