# CYD Cyberdeck

5-in-1 cyberdeck dashboard for the ESP32-2432S028R (Cheap Yellow Display / CYD 2.8").

Modes cycle automatically every 60 seconds, or tap the touchscreen to switch manually.

## Modes

1. **Clock** — digital clock with rainbow second-progress bar and WiFi signal indicator
2. **Weather** — temperature, humidity, and wind speed via Open-Meteo API
3. **Crypto** — live prices for 9 coins (BTC, ETH, BNB, SOL, XRP, ADA, TRX, DOGE, SHIB) via CoinGecko
4. **Matrix** — falling green character rain animation
5. **Game of Life** — Conway's cellular automaton with color-shifting cells

## Hardware

- **Board:** ESP32-2432S028R (CYD 2.8")
- **Display:** 2.8" ILI9341 320×240 TFT
- **Touch:** XPT2046 resistive touchscreen
- **Connectivity:** WiFi (2.4 GHz)

## Build & Flash

Requires [PlatformIO](https://platformio.org/).

```bash
# Compile and flash (CYD connected via USB)
pio run -e cyd -t upload

# Monitor serial output
pio device monitor
```

## WiFi Setup

On first boot (or after a reset), the CYD creates an access point:

1. Connect your phone/laptop to WiFi network **`CYD-Cyberdeck`**
2. A captive portal opens automatically (or browse to `192.168.4.1`)
3. Enter your WiFi SSID and password, save
4. The CYD reboots and connects to your network

Credentials are stored in flash — subsequent boots auto-connect.
The portal times out after 5 minutes and restarts the device.

## Configuration

Edit `src/main.cpp` to change:

```cpp
String LAT = "41.03";   // Your latitude
String LON = "21.33";   // Your longitude
```

These coordinates are used for the weather display.

## Display Color Fix

Some CYD units have inverted display panels. If colors look wrong,
toggle the inversion in `setup()`:

```cpp
tft.invertDisplay(true);   // try false if colors are inverted
```

## Project Structure

```
├── platformio.ini    # Build config, pin definitions, library deps
└── src/
    └── main.cpp      # All firmware source
```

## Credits

Based on the "5-in-1 Cyberdeck CYD 2.8inch" project.
