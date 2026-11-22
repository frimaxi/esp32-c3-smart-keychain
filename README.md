# esp32-c3-smart-keychain

![Platform](https://img.shields.io/badge/Platform-ESP32--C3-blue)
![Framework](https://img.shields.io/badge/Framework-Arduino%20%2F%20PlatformIO-orange)
![Display](https://img.shields.io/badge/Display-ST7789%20240x240-lightgrey)

A single-button **ESP32-C3 smart keychain** featuring an animated chick mascot, a persistent internal RTC clock, and a ~33 FPS Dino runner mini-game on a 1.3" IPS display.

---

## Features

* **Single-Button Interface:** Navigate menus, jump, set the time, cycle expressions, and wake/sleep the keychain using a single push button (`< 400 ms` short click vs. `>= 400 ms` long press, with 50 ms hardware interrupt debouncing).
* **Dino Run Mini-Game:** Smooth, flicker-free rendering (~33 FPS) powered by an off-screen `GFXcanvas1` RAM buffer, progressive speed scaling, and persistent high score across Deep Sleep cycles (`RTC_DATA_ATTR`).
* **Internal RTC Clock:** Keeps accurate time even while in Deep Sleep (as long as the LiPo battery remains connected) with an intuitive single-button hours/minutes setup mode.
* **Animated Mascot (9 Expressions):** Renders `160x160` 16-bit RGB565 bitmaps stored in Flash (`PROGMEM`), alternating automatically between idle reactions or cycling manually on click.
* **Ultra-Low Power Deep Sleep:** Disables Wi-Fi and Bluetooth radios, puts the ST7789 display controller to sleep, locks the backlight GPIO `LOW` during sleep, and wakes up instantly when `D0` is pressed.

---

## Hardware & Bill of Materials

* **Microcontroller:** Seeed Studio XIAO ESP32-C3
* **Display:** 1.3" IPS TFT LCD (`240x240`, ST7789 driver, SPI interface)
* **Input:** 1x Momentary tactile push button
* **Power:** 3.7V LiPo battery (e.g., 250 mAh) soldered to the XIAO battery pads

---

## Wiring / Pinout

The display uses the XIAO ESP32-C3 hardware SPI bus (`D8` / `D10`) alongside dedicated control pins:

```text
Seeed XIAO ESP32-C3             1.3" ST7789 IPS (240x240)
┌─────────────────┐             ┌───────────────────────┐
│             3V3 ├────────────►│ VCC                   │
│             GND ├────────────►│ GND                   │
│       D8 (SCK)  ├────────────►│ SCL                   │
│       D10 (MOSI)├────────────►│ SDA                   │
│       D5        ├────────────►│ CS  (optional)        │
│       D4        ├────────────►│ DC                    │
│       D3        ├────────────►│ RST                   │
│       D2        ├────────────►│ BLK (Backlight)       │
│                 │             └───────────────────────┘
│       D0        ├───┐
│             GND ├───┴─[ Push Button ]
└─────────────────┘
```

| Component Pin | XIAO ESP32-C3 Pin | Notes |
| :--- | :--- | :--- |
| **TFT VCC** | `3V3` | 3.3V power supply |
| **TFT GND** | `GND` | Common ground |
| **TFT SCL** |
