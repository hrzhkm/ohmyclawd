// src/capture.h
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include "globals.h"

namespace capture {

static TFT_eSprite* sprite = nullptr;
static uint32_t     frameNum = 0;
static unsigned long virtualMs = 0;
static const unsigned long FRAME_STEP_MS = 50; // 20fps
static String       sinkUrl = "";
static TaskHandle_t sendTaskHandle = nullptr;
static volatile bool sendBusy = false;

#define recording captureRecording
#define frameReady captureFrameReady

inline bool isActive() { return recording && sprite != nullptr; }
inline void markFrame() { frameReady = true; }

// Forward declaration
inline void sendFrame();

// Background task: runs on core 0, sends frames without blocking main loop
static void sendTaskFunc(void* param) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    sendBusy = true;
    sendFrame();
    sendBusy = false;
  }
}

inline bool init() {
  if (sprite) return true;
  sprite = new TFT_eSprite(&tft);
  sprite->setColorDepth(8);
  if (!sprite->createSprite(240, 320)) {
    delete sprite;
    sprite = nullptr;
    Serial.println("[capture] sprite alloc FAILED");
    return false;
  }
  Serial.printf("[capture] sprite OK, heap=%u\n", ESP.getFreeHeap());

  if (!sendTaskHandle) {
    xTaskCreatePinnedToCore(sendTaskFunc, "cap_send", 8192, nullptr, 1, &sendTaskHandle, 0);
  }
  return true;
}

inline void deinit() {
  if (sprite) { sprite->deleteSprite(); delete sprite; sprite = nullptr; }
  recording = false;
  canvas = &tft;
}

inline void startRecording(const String& url) {
  if (!sprite && !init()) return;
  sinkUrl = url;
  frameNum = 0;
  virtualMs = millis();
  recording = true;
  frameReady = true;
  sendBusy = false;
  canvas = sprite;
  lastSpriteFrame = 0;
  modeChanged = true;
  Serial.printf("[capture] recording to %s\n", url.c_str());
}

inline void stopRecording() {
  recording = false;
  canvas = &tft;
  Serial.printf("[capture] stopped, %u frames\n", frameNum);
}

inline unsigned long now() {
  if (recording) return virtualMs;
  return millis();
}

inline void advanceClock() {
  virtualMs += FRAME_STEP_MS;
  lastSpriteFrame = 0;
}

inline void sendFrame() {
  if (!sprite || sinkUrl.length() == 0) return;

  const int W = 240, H = 320;
  const int rowBytes = W * 3;
  const int padRow = (4 - (rowBytes % 4)) % 4;
  const int imgSize = (rowBytes + padRow) * H;
  const int fileSize = 54 + imgSize;

  uint8_t hdr[54] = {0};
  hdr[0] = 'B'; hdr[1] = 'M';
  hdr[2] = fileSize & 0xFF; hdr[3] = (fileSize >> 8) & 0xFF;
  hdr[4] = (fileSize >> 16) & 0xFF; hdr[5] = (fileSize >> 24) & 0xFF;
  hdr[10] = 54;
  hdr[14] = 40;
  hdr[18] = W & 0xFF; hdr[19] = (W >> 8) & 0xFF;
  int32_t negH = -H;
  memcpy(&hdr[22], &negH, 4);
  hdr[26] = 1;
  hdr[28] = 24;
  hdr[34] = imgSize & 0xFF; hdr[35] = (imgSize >> 8) & 0xFF;
  hdr[36] = (imgSize >> 16) & 0xFF; hdr[37] = (imgSize >> 24) & 0xFF;

  WiFiClient client;
  int port = 8788;
  String host = sinkUrl;
  if (host.startsWith("http://")) host = host.substring(7);
  int colonIdx = host.indexOf(':');
  if (colonIdx > 0) { port = host.substring(colonIdx + 1).toInt(); host = host.substring(0, colonIdx); }
  int slashIdx = host.indexOf('/');
  if (slashIdx > 0) host = host.substring(0, slashIdx);

  if (!client.connect(host.c_str(), port, 1000)) {
    Serial.println("[capture] connect failed");
    return;
  }

  client.printf("POST /frame HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: image/bmp\r\n", host.c_str(), port);
  client.printf("Content-Length: %d\r\nX-Frame: %u\r\nConnection: close\r\n\r\n", fileSize, frameNum);
  client.write(hdr, 54);

  uint8_t rowBuf[rowBytes + 4];
  memset(rowBuf + rowBytes, 0, padRow);
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      uint16_t c = sprite->readPixel(x, y);
      rowBuf[x * 3 + 0] = (c & 0x001F) << 3;
      rowBuf[x * 3 + 1] = ((c >> 5) & 0x003F) << 2;
      rowBuf[x * 3 + 2] = ((c >> 11) & 0x001F) << 3;
    }
    client.write(rowBuf, rowBytes + padRow);
  }

  client.stop();
  frameNum++;
  advanceClock();
}

// Push sprite to display (keeps screen live)
inline void flush() {
  if (recording && sprite) sprite->pushSprite(0, 0);
}

// Called every loop — dispatches frame send to background task
inline void endFrame() {
  if (!recording || !sprite) return;
  if (!frameReady || sendBusy) return;
  frameReady = false;
  xTaskNotifyGive(sendTaskHandle);
}

#undef recording
#undef frameReady

} // namespace capture
