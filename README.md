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
| **TFT SCL** | `D8` (GPIO8) | Hardware SPI Clock |
| **TFT SDA** | `D10` (GPIO10) | Hardware SPI MOSI |
| **TFT CS** | `D5` (GPIO7) | Chip Select *(leave unconnected if module has no CS pin)* |
| **TFT DC** | `D4` (GPIO6) | Data / Command |
| **TFT RST** | `D3` (GPIO5) | Hardware Reset |
| **TFT BLK** | `D2` (GPIO4) | Backlight control (held `LOW` during Deep Sleep) |
| **Button** | `D0` (GPIO2) & `GND` | Active `LOW` (`INPUT_PULLUP` + GPIO Deep Sleep wake-up) |

---

## Controls

| Screen / Mode | Short Click (`< 400 ms`) | Long Press (`>= 400 ms`) |
| :--- | :--- | :--- |
| **Main Menu** | Move selection down (`Jeu` / `Heure` / `Smiley` / `Eteindre`) | Open selected app / Enter Deep Sleep |
| **Dino Run** | Start game / Jump / Restart after Game Over | Return to Main Menu |
| **RTC Clock** | Enter time adjustment / Increment value (`+1`) | Confirm Hours -> Minutes -> Save & Exit / Return to Menu |
| **Mascot (`Smiley`)** | Manually cycle through all 9 expressions | Return to Main Menu |
| **Deep Sleep** | Wake up device | Wake up device |

---

## How to Build & Upload (Flash) the Firmware

### 1. Prerequisites
* Install **[Visual Studio Code](https://code.visualstudio.com/)**.
* Install the **PlatformIO IDE** extension from the VS Code Extensions Marketplace.
* Use a **USB-C data cable** (charge-only cables will not detect the board).

### 2. Clone & Open the Project
```bash
git clone [https://github.com/frimaxi/esp32-c3-smart-keychain.git](https://github.com/frimaxi/esp32-c3-smart-keychain.git)
cd esp32-c3-smart-keychain
```
Open the folder in VS Code (`File > Open Folder...`). PlatformIO will automatically read `platformio.ini`, download the Espressif32 toolchain, install `Adafruit GFX Library` and `Adafruit ST7789 Library`, and configure the `huge_app.csv` (3 MB) Flash partition.

### 3. Upload to the XIAO ESP32-C3
1. Plug the XIAO ESP32-C3 into your computer via USB-C.
2. In the bottom blue PlatformIO toolbar in VS Code:
   * Click the **Checkmark icon (`Build`)** to verify that the code compiles cleanly.
   * Click the **Right Arrow icon (`Upload`)** to flash the firmware onto the board (or run `pio run --target upload` in the PlatformIO terminal).
3. **Troubleshooting Upload / Bootloader Mode:** If the upload fails or the COM port is not recognized:
   * Hold down the **`BOOT` (B)** button on the XIAO ESP32-C3.
   * While holding `BOOT`, press and release the **`RESET` (R)** button.
   * Release the **`BOOT`** button, then click **Upload** again.

### 4. First Boot Behavior
Upon initial power-up or flashing, the firmware immediately enters **Deep Sleep** to conserve battery power (the screen stays off). **Press the `D0` button once** to wake the microcontroller and display the Main Menu.

---

## How to Create Custom Character Sprites & Generate `images_poussin.h`

The mascot mode displays `160x160` 16-bit color (RGB565) sprites stored directly in the ESP32-C3 Flash memory (`PROGMEM`) inside `include/images_poussin.h`. To create a seamless full-screen effect on the `240x240` display, the entire screen is filled with the exact background color of the sprite sheet (`FOND_JAUNE`).

### Image Design Rules
* **Layout:** Prepare a single image containing **9 expressions arranged in a 3x3 grid** (3 columns × 3 rows), or 9 individual square images.
* **Background:** Use a **solid, uniform background color** around the character (avoid gradients or checkered transparency patterns so the `160x160` sprite blends invisibly into the `240x240` screen).
* **Resolution:** Each expression is scaled proportionally into a **`160x160` pixel** square (`25,600` pixels per sprite, ~460 KB total for 9 sprites).

---

### Method A: Automated Generation with Python (Recommended)

The included `convertir.py` script automatically slices a 3x3 grid, resizes each cell proportionally without stretching, extracts the exact RGB565 background color, and generates `images_poussin.h`.

1. **Install Pillow (Python imaging library):**
   ```bash
   pip install pillow
   ```
2. **Add your sprite sheet:**
   * Place your 3x3 image in the root folder of the project and name it `Ania.jpg` (or edit `Image.open("Ania.jpg")` in `convertir.py`).
3. **Run the conversion script:**
   ```bash
   python convertir.py
   ```
4. **Install the generated header & match the background color:**
   * Move the generated `images_poussin.h` file into the `include/` folder (replacing the existing one).
   * Look at the terminal output — the script prints the exact hex color code of the top-left background pixel (e.g., `#define FOND_JAUNE 0xF72F`).
   * Copy that value into `#define FOND_JAUNE` at the top of `src/main.cpp`, then re-upload the firmware.

---

### Method B: Manual Conversion via Browser (No Python Required)

If you prefer not to use Python, you can generate the C++ arrays online:

1. **Split the 3x3 grid:** Use an online image splitter (such as *PineTools Split Image*) set to **3 columns × 3 rows** to export 9 separate image files.
2. **Convert each image to RGB565:** Open **[image2cpp](https://javl.github.io/image2cpp/)** and load each expression one by one with these exact settings:
   * **Canvas size:** `160` × `160`
   * **Scaling:** `Scale to fit, keeping proportions`
   * **Background color:** Match your sprite's background color
   * **Code output format:** `Arduino code`
   * **Draw mode:** `Horizontal - 2 bytes per pixel (565)`
3. **Update `include/images_poussin.h`:**
   * Paste the generated `{ 0x...., ... }` hex values into the 9 `PROGMEM` arrays inside `include/images_poussin.h`.
   * Copy the very first hex value of the array (top-left pixel) into `#define FOND_JAUNE` inside `src/main.cpp`.

---

### Structure of `include/images_poussin.h`

If you want to add or remove expressions (e.g., using 6 or 12 sprites instead of 9), keep the following structure in `include/images_poussin.h`:

```cpp
#pragma once
#include <Arduino.h>

const int IMG_LARGEUR = 160;
const int IMG_HAUTEUR = 160;

// Individual 160x160 RGB565 sprite arrays stored in Flash (PROGMEM)
const uint16_t poussin_clin[]   PROGMEM = { /* 25,600 uint16_t values */ };
const uint16_t poussin_joie[]   PROGMEM = { /* 25,600 uint16_t values */ };
// ... (remaining sprite arrays)

// Lookup table used by src/main.cpp to index expressions 0..(NB_EXPRESSIONS - 1)
const uint16_t* const IMAGES_POUSSIN[] PROGMEM = {
  poussin_clin,    // Index 0: Default idle expression
  poussin_joie,    // Index 1..4: Automatic idle reaction cycle
  poussin_coeurs,
  poussin_rire,
  poussin_timide,
  poussin_choque,  // Index 5..8: Accessible via manual short-click cycling
  poussin_colere,
  poussin_pleure,
  poussin_dodo
};

const int NB_EXPRESSIONS = 9;
```

---

## Project Structure

```text
esp32-c3-smart-keychain/
├── include/
│   └── images_poussin.h   # 9 character sprites (160x160 RGB565 arrays in PROGMEM)
├── src/
│   └── main.cpp           # State machine, Dino game engine, RTC clock, Deep Sleep
├── convertir.py           # Python script to slice & convert a 3x3 image grid to RGB565
├── platformio.ini         # Board configuration, Flash partition table, and dependencies
├── .gitignore             # Excludes build artifacts and local source images
└── README.md              # Project documentation
```
