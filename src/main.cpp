#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include "driver/gpio.h"
#include "images_poussin.h" // 160x160 RGB565 character sprites stored in Flash (PROGMEM)

// ----------------------------------------------------------------------------
// 1. HARDWARE PINOUT (Seeed Studio XIAO ESP32-C3)
// ----------------------------------------------------------------------------
// Note: Using the 3-argument constructor relies on hardware SPI:
//       SCL -> D8 (GPIO8) | SDA -> D10 (GPIO10)
#define TFT_CS   D5
#define TFT_DC   D4
#define TFT_RST  D3
#define TFT_BLK  D2  // Backlight control pin (held LOW during Deep Sleep)

#define BTN_PIN  D0  // Wake-up capable GPIO pin (configured with INPUT_PULLUP)

// ----------------------------------------------------------------------------
// 2. COLOR PALETTE (16-bit RGB565 format)
// ----------------------------------------------------------------------------
// Customize your UI theme here
#define NOIR           0x0000  // Black
#define BLEU_CLAIR     0x96BF  // Light Blue (Primary UI accent)
#define JAUNE          0xFFE0  // Yellow
#define ORANGE         0xFD20  // Orange
#define BLANC          0xFFFF  // White
#define FOND_JAUNE     0xF72F  // Exact background yellow matching the sprite sheet
#define FOND_Maron     0xF612  // Warm beige/brown accent

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ----------------------------------------------------------------------------
// 3. STATE MACHINES & GLOBAL VARIABLES
// ----------------------------------------------------------------------------
// Main application screens
enum Etat {
  ETAT_MENU,
  ETAT_SMILEY,
  ETAT_HORLOGE,
  ETAT_JEU
};

// Clock sub-states for single-button time adjustment
enum ModeHorloge {
  AFFICHAGE_HEURE,
  REGLAGE_HEURES,
  REGLAGE_MINUTES
};

// Dino mini-game sub-states
enum ModeJeu {
  JEU_PRET,
  JEU_EN_COURS,
  JEU_GAME_OVER
};

// Forward declaration needed by menu()
void entrerEnVeilleProfonde();

// Interrupt-shared variables (must be volatile)
volatile bool boutonAppuye = false;
volatile unsigned long tempsAppuye = 0;

// UI navigation & dirty-flag rendering (prevents SPI screen flicker)
int selection = 0;
Etat etatActuel = ETAT_MENU;
bool majEcran = true;

// 1-bit off-screen buffer in RAM (~7.2 KB) for flicker-free 30 FPS game rendering
GFXcanvas1 canvas(240, 240);

// ----------------------------------------------------------------------------
// 4. DINO GAME CONFIGURATION & STATE
// ----------------------------------------------------------------------------
const int SOL_Y    = 190; // Ground Y coordinate
const int DINO_X   = 32;  // Fixed horizontal position of the Dino
const int DINO_L   = 22;  // Dino hitbox width
const int DINO_H   = 26;  // Dino hitbox height
const int CACTUS_L = 14;  // Cactus width

ModeJeu modeJeu = JEU_PRET;
float dinoY = SOL_Y - DINO_H;
float vitesseY = 0;
bool auSol = true;
float cactusX = 250;
int cactusH = 28;
float vitesseJeu = 5.5f;
int score = 0;
RTC_DATA_ATTR int meilleurScore = 0; // Persists in RTC memory across Deep Sleep cycles
unsigned long momentGameOver = 0;

// ----------------------------------------------------------------------------
// 5. BUTTON INTERRUPT SERVICE ROUTINE (ISR)
// ----------------------------------------------------------------------------
// Measures press duration on release and filters out mechanical switch bounce (< 50 ms)
void IRAM_ATTR gererAppuiBouton() {
  static unsigned long temps1 = 0;
  static unsigned long temps2 = 0;

  if (digitalRead(BTN_PIN) == LOW) {
    temps1 = millis();
  } else if (digitalRead(BTN_PIN) == HIGH) {
    temps2 = millis();
    unsigned long temp = temps2 - temps1;
    if (temp > 50) { // Software debounce threshold
      tempsAppuye = temp;
      boutonAppuye = true;
    }
  }
}

// ----------------------------------------------------------------------------
// 6. MAIN MENU
// ----------------------------------------------------------------------------
void menu() {
  const char* options[] = {"Jeu", "Heure", "Smiley", "Eteindre"};

  // Handle button input
  if (boutonAppuye) {
    if (tempsAppuye < 400) {
      // Short click: cycle through menu options
      selection = (selection + 1) % 4;
      majEcran = true;
    } else {
      // Long press (>= 400 ms): launch selected mode
      tft.fillScreen(NOIR);
      majEcran = true;
      switch (selection) {
        case 0: etatActuel = ETAT_JEU;     break;
        case 1: etatActuel = ETAT_HORLOGE; break;
        case 2: etatActuel = ETAT_SMILEY;  break;
        case 3: entrerEnVeilleProfonde();  break;
      }
    }
    boutonAppuye = false;
  }

  // Render UI only when a state or selection change occurred
  if (majEcran && etatActuel == ETAT_MENU) {
    // Centered header and horizontal divider
    tft.setTextSize(3);
    tft.setTextColor(BLEU_CLAIR, NOIR);
    tft.setCursor(84, 14);
    tft.print("Menu");
    tft.fillRect(20, 44, 200, 3, BLEU_CLAIR);

    // Draw menu items with a rounded highlight pill on the active row
    tft.setTextSize(2);
    for (int i = 0; i < 4; i++) {
      int y = 58 + (i * 44);

      if (i == selection) {
        tft.fillRoundRect(24, y, 192, 36, 8, BLEU_CLAIR);
        tft.setTextColor(NOIR, BLEU_CLAIR);
      } else {
        tft.fillRoundRect(24, y, 192, 36, 8, NOIR);
        tft.setTextColor(BLANC, NOIR);
      }

      tft.setCursor(42, y + 10);
      tft.print(options[i]);
    }

    majEcran = false;
  }
}

// ----------------------------------------------------------------------------
// 7. DINO MINI-GAME ENGINE
// ----------------------------------------------------------------------------
void reinitialiserJeu() {
  dinoY = SOL_Y - DINO_H;
  vitesseY = 0;
  auSol = true;
  cactusX = 250;
  cactusH = 28;
  vitesseJeu = 5.5f;
  score = 0;
  boutonAppuye = false;
}

void lancejeu() {
  static unsigned long derniereFrame = 0;
  static int decalageSol = 0;
  static bool jambeAlternee = false;
  static int compteurPas = 0;

  // Lock frame rate to ~33 FPS (30 ms per frame)
  if (millis() - derniereFrame < 30) {
    return;
  }
  derniereFrame = millis();

  // 1. Zero-latency jump: read raw GPIO state directly instead of waiting for button release
  if (digitalRead(BTN_PIN) == LOW && auSol) {
    vitesseY = -11.5f; // Initial upward impulse
    auSol = false;
  }

  // 2. Vertical physics (gravity & ground collision)
  dinoY += vitesseY;
  vitesseY += 0.85f; // Gravity acceleration
  if (dinoY >= SOL_Y - DINO_H) {
    dinoY = SOL_Y - DINO_H;
    vitesseY = 0;
    auSol = true;
  }

  // 3. Obstacle movement, random respawn & progressive speed scaling
  cactusX -= vitesseJeu;
  if (cactusX < -CACTUS_L) {
    cactusX = 240 + random(15, 95);
    cactusH = random(22, 34);
    score++;
    if (score > meilleurScore) {
      meilleurScore = score;
    }
    if (vitesseJeu < 11.0f) {
      vitesseJeu += 0.15f; // Gradually increase difficulty
    }
  }

  // 4. AABB collision detection (with a forgiving 3 px margin for fair gameplay)
  int dY = (int)dinoY;
  int cX = (int)cactusX;
  int cY = SOL_Y - cactusH;

  bool collision = (DINO_X + DINO_L - 4 > cX + 2) &&
                   (DINO_X + 4 < cX + CACTUS_L - 2) &&
                   (dY + DINO_H - 3 > cY + 2) &&
                   (dY + 3 < SOL_Y);

  if (collision) {
    modeJeu = JEU_GAME_OVER;
    momentGameOver = millis();
    boutonAppuye = false;
    majEcran = true;
    return;
  }

  // 5. Render full scene into the off-screen RAM canvas
  canvas.fillScreen(0);

  // HUD: High Score & Current Score
  canvas.setTextSize(2);
  canvas.setTextColor(1);
  canvas.setCursor(10, 12);
  canvas.printf("HI %03d", meilleurScore);
  canvas.setCursor(155, 12);
  canvas.printf("%03d", score);

  // Scrolling ground line and pebbles
  canvas.drawFastHLine(0, SOL_Y, 240, 1);
  decalageSol = (decalageSol + (int)vitesseJeu) % 40;
  for (int x = -decalageSol; x < 240; x += 40) {
    canvas.drawFastHLine(x + 8, SOL_Y + 5, 10, 1);
    canvas.drawFastHLine(x + 26, SOL_Y + 9, 5, 1);
  }

  // Procedural T-Rex sprite
  canvas.fillRect(DINO_X + 8, dY, 14, 10, 1);       // Head
  canvas.fillRect(DINO_X + 16, dY + 2, 2, 2, 0);    // Eye
  canvas.fillRect(DINO_X + 4, dY + 9, 13, 11, 1);   // Torso
  canvas.fillRect(DINO_X, dY + 11, 4, 6, 1);        // Tail
  canvas.fillRect(DINO_X + 17, dY + 12, 4, 2, 1);   // Arm

  // Leg animation (tucked while jumping, alternating while running)
  if (!auSol) {
    canvas.fillRect(DINO_X + 6, dY + 20, 4, 6, 1);
    canvas.fillRect(DINO_X + 12, dY + 20, 4, 6, 1);
  } else {
    compteurPas++;
    if (compteurPas >= 4) {
      jambeAlternee = !jambeAlternee;
      compteurPas = 0;
    }
    if (jambeAlternee) {
      canvas.fillRect(DINO_X + 6, dY + 20, 4, 6, 1);
      canvas.fillRect(DINO_X + 12, dY + 20, 4, 3, 1);
    } else {
      canvas.fillRect(DINO_X + 6, dY + 20, 4, 3, 1);
      canvas.fillRect(DINO_X + 12, dY + 20, 4, 6, 1);
    }
  }

  // Procedural Cactus sprite
  canvas.fillRect(cX + 4, cY, 6, cactusH, 1);
  canvas.fillRect(cX, cY + 7, 4, 3, 1);
  canvas.fillRect(cX, cY + 3, 2, 7, 1);
  canvas.fillRect(cX + 10, cY + 10, 4, 3, 1);
  canvas.fillRect(cX + 12, cY + 5, 2, 8, 1);

  // 6. Push entire frame buffer to the ST7789 over SPI in a single draw call
  tft.drawBitmap(0, 0, canvas.getBuffer(), 240, 240, BLEU_CLAIR, NOIR);
}

void jeu() {
  // Sub-state 1: Title screen
  if (modeJeu == JEU_PRET) {
    if (majEcran) {
      tft.fillScreen(NOIR);
      tft.setTextSize(3);
      tft.setTextColor(BLEU_CLAIR, NOIR);
      tft.setCursor(48, 35);
      tft.print("DINO RUN");

      // Static Dino preview on the ground
      tft.drawFastHLine(0, SOL_Y, 240, BLEU_CLAIR);
      tft.fillRect(DINO_X + 8, SOL_Y - DINO_H, 14, 10, BLEU_CLAIR);
      tft.fillRect(DINO_X + 16, SOL_Y - DINO_H + 2, 2, 2, NOIR);
      tft.fillRect(DINO_X + 4, SOL_Y - DINO_H + 9, 13, 11, BLEU_CLAIR);
      tft.fillRect(DINO_X, SOL_Y - DINO_H + 11, 4, 6, BLEU_CLAIR);
      tft.fillRect(DINO_X + 6, SOL_Y - 6, 4, 6, BLEU_CLAIR);
      tft.fillRect(DINO_X + 12, SOL_Y - 6, 4, 6, BLEU_CLAIR);

      tft.setTextSize(2);
      tft.setTextColor(BLANC, NOIR);
      tft.setCursor(42, 105);
      tft.print("Clic: Jouer");

      tft.setTextSize(1);
      tft.setCursor(35, 212);
      tft.print("Appui long: Retour Menu");
      majEcran = false;
    }

    if (boutonAppuye) {
      if (tempsAppuye >= 400) {
        etatActuel = ETAT_MENU;
        tft.fillScreen(NOIR);
        majEcran = true;
      } else {
        reinitialiserJeu();
        modeJeu = JEU_EN_COURS;
      }
      boutonAppuye = false;
    }
  } 
  // Sub-state 2: Active gameplay
  else if (modeJeu == JEU_EN_COURS) {
    lancejeu();
  } 
  // Sub-state 3: Game Over modal
  else if (modeJeu == JEU_GAME_OVER) {
    if (majEcran) {
      tft.fillRect(25, 55, 190, 115, NOIR);
      tft.drawRoundRect(25, 55, 190, 115, 8, BLEU_CLAIR);

      tft.setTextSize(2);
      tft.setTextColor(ORANGE, NOIR);
      tft.setCursor(66, 68);
      tft.print("GAME OVER");

      tft.setTextColor(BLANC, NOIR);
      tft.setCursor(55, 98);
      tft.printf("Score : %03d", score);
      tft.setCursor(55, 120);
      tft.printf("Record: %03d", meilleurScore);

      tft.setTextSize(1);
      tft.setTextColor(BLEU_CLAIR, NOIR);
      tft.setCursor(38, 148);
      tft.print("Court: Rejouer | Long: Menu");
      majEcran = false;
    }

    if (boutonAppuye) {
      // 400 ms lockout prevents the player's last jump release from accidentally triggering menu exit
      if (millis() - momentGameOver > 400) {
        if (tempsAppuye >= 400) {
          modeJeu = JEU_PRET;
          etatActuel = ETAT_MENU;
          tft.fillScreen(NOIR);
          majEcran = true;
        } else {
          reinitialiserJeu();
          modeJeu = JEU_EN_COURS;
        }
      }
      boutonAppuye = false;
    }
  }
}

// ----------------------------------------------------------------------------
// 8. INTERNAL RTC CLOCK (Persists during Deep Sleep while powered)
// ----------------------------------------------------------------------------
void horloge() {
  static ModeHorloge mode = AFFICHAGE_HEURE;
  static int heuresReglage = 0;
  static int minutesReglage = 0;
  static unsigned long dernierRafraichissement = 0;

  // Read current time from the ESP32 internal RTC
  time_t maintenant;
  time(&maintenant);
  struct tm temps;
  localtime_r(&maintenant, &temps);

  // Handle button inputs across clock modes
  if (boutonAppuye) {
    if (mode == AFFICHAGE_HEURE) {
      if (tempsAppuye >= 400) {
        // Long press: exit to Main Menu
        etatActuel = ETAT_MENU;
        tft.fillScreen(NOIR);
        majEcran = true;
        boutonAppuye = false;
        return;
      } else {
        // Short click: enter Hours adjustment mode
        heuresReglage = temps.tm_hour;
        minutesReglage = temps.tm_min;
        mode = REGLAGE_HEURES;
        tft.fillScreen(NOIR);
        majEcran = true;
      }
    } else if (mode == REGLAGE_HEURES) {
      if (tempsAppuye < 400) {
        heuresReglage = (heuresReglage + 1) % 24;
        majEcran = true;
      } else {
        // Long press: confirm hours, move to Minutes adjustment
        mode = REGLAGE_MINUTES;
        majEcran = true;
      }
    } else if (mode == REGLAGE_MINUTES) {
      if (tempsAppuye < 400) {
        minutesReglage = (minutesReglage + 1) % 60;
        majEcran = true;
      } else {
        // Long press: save new time into the system RTC
        struct tm nouveauTemps = {0};
        nouveauTemps.tm_year = 2026 - 1900; // Valid reference year required by mktime()
        nouveauTemps.tm_mon  = 0;
        nouveauTemps.tm_mday = 1;
        nouveauTemps.tm_hour = heuresReglage;
        nouveauTemps.tm_min  = minutesReglage;
        nouveauTemps.tm_sec  = 0;

        time_t secondes = mktime(&nouveauTemps);
        struct timeval tv = { .tv_sec = secondes, .tv_usec = 0 };
        settimeofday(&tv, NULL);

        mode = AFFICHAGE_HEURE;
        tft.fillScreen(NOIR);
        majEcran = true;
      }
    }
    boutonAppuye = false;
  }

  // Trigger a screen update once per second in normal clock mode
  if (mode == AFFICHAGE_HEURE && (millis() - dernierRafraichissement >= 1000)) {
    majEcran = true;
  }

  // Render clock UI
  if (majEcran) {
    dernierRafraichissement = millis();

    tft.setTextSize(2);
    tft.setTextColor(BLEU_CLAIR, NOIR);
    tft.setCursor(10, 20);
    if (mode == AFFICHAGE_HEURE) {
      tft.print("Horloge       ");
    } else if (mode == REGLAGE_HEURES) {
      tft.print("Reglage Heures");
    } else {
      tft.print("Reglage Min.  ");
    }

    int hAffichage = (mode == AFFICHAGE_HEURE) ? temps.tm_hour : heuresReglage;
    int mAffichage = (mode == AFFICHAGE_HEURE) ? temps.tm_min  : minutesReglage;

    tft.setTextSize(4);
    tft.setCursor(60, 95);

    // Highlight hours when active
    if (mode == REGLAGE_HEURES) {
      tft.setTextColor(NOIR, BLANC);
    } else {
      tft.setTextColor(BLANC, NOIR);
    }
    tft.printf("%02d", hAffichage);

    tft.setTextColor(BLEU_CLAIR, NOIR);
    tft.print(":");

    // Highlight minutes when active
    if (mode == REGLAGE_MINUTES) {
      tft.setTextColor(NOIR, BLANC);
    } else {
      tft.setTextColor(BLANC, NOIR);
    }
    tft.printf("%02d", mAffichage);

    // Seconds counter (hidden during time adjustment)
    tft.setTextSize(2);
    tft.setCursor(108, 140);
    if (mode == AFFICHAGE_HEURE) {
      tft.setTextColor(BLEU_CLAIR, NOIR);
      tft.printf("%02d", temps.tm_sec);
    } else {
      tft.setTextColor(NOIR, NOIR);
      tft.print("  ");
    }

    // Footer control hint
    tft.setTextSize(1);
    tft.setTextColor(BLANC, NOIR);
    tft.setCursor(15, 210);
    if (mode == AFFICHAGE_HEURE) {
      tft.print("Clic court: Regler | Long: Menu");
    } else {
      tft.print("Clic court: +1     | Long: Valider");
    }

    majEcran = false;
  }
}

// ----------------------------------------------------------------------------
// 9. ANIMATED MASCOT / CHARACTER DISPLAY (PROGMEM RGB565 Bitmaps)
// ----------------------------------------------------------------------------
// Centers and draws the selected 160x160 sprite from Flash memory
void afficherExpression(int index) {
  int posX = (240 - IMG_LARGEUR) / 2;
  int posY = (240 - IMG_HAUTEUR) / 2;
  const uint16_t* img = (const uint16_t*)pgm_read_ptr(&IMAGES_POUSSIN[index]);
  tft.drawRGBBitmap(posX, posY, img, IMG_LARGEUR, IMG_HAUTEUR);
}

void dessinerSmiley() {
  static int indexActuel = 0;
  static unsigned long dernierChangement = 0;
  static unsigned long dureeMaintien = 3500;
  static int indexAuto = 1;

  // 1. Handle button input
  if (boutonAppuye) {
    if (tempsAppuye >= 400) {
      // Long press: return to Main Menu and restore black background
      etatActuel = ETAT_MENU;
      tft.fillScreen(NOIR);
      majEcran = true;
      boutonAppuye = false;
      return;
    } else {
      // Short click: manually cycle through all 9 character expressions
      indexActuel = (indexActuel + 1) % NB_EXPRESSIONS;
      afficherExpression(indexActuel);
      dernierChangement = millis();
      dureeMaintien = 4000; // Hold user-selected expression for 4 seconds
    }
    boutonAppuye = false;
  }

  // 2. Initial screen draw (fills screen with FOND_JAUNE so 160x160 sprites blend seamlessly)
  if (majEcran) {
    tft.fillScreen(FOND_JAUNE);
    indexActuel = 0;
    afficherExpression(indexActuel);
    dernierChangement = millis();
    dureeMaintien = 3500;
    majEcran = false;
  }

  // 3. Idle animation loop (periodically cycles through happy expressions 1..4)
  unsigned long maintenant = millis();
  if (maintenant - dernierChangement >= dureeMaintien) {
    if (indexActuel == 0) {
      indexActuel = indexAuto;
      indexAuto++;
      if (indexAuto > 4) indexAuto = 1;
      dureeMaintien = 900;  // Show reaction for 0.9 s
    } else {
      indexActuel = 0;      // Return to default expression
      dureeMaintien = 3500; // Wait 3.5 s before next reaction
    }
    afficherExpression(indexActuel);
    dernierChangement = maintenant;
  }
}

// ----------------------------------------------------------------------------
// 10. POWER MANAGEMENT (DEEP SLEEP)
// ----------------------------------------------------------------------------
void entrerEnVeilleProfonde() {
  // Cut display backlight and lock GPIO state LOW during Deep Sleep
  digitalWrite(TFT_BLK, LOW);
  gpio_hold_en((gpio_num_t)TFT_BLK);
  gpio_deep_sleep_hold_en();

  // Put ST7789 display controller into low-power sleep mode
  tft.enableDisplay(false);
  tft.enableSleep(true);

  // Wait for the user to release D0 so the device doesn't immediately wake back up
  while (digitalRead(BTN_PIN) == LOW) {
    delay(10);
  }
  delay(50);

  // Arm GPIO wake-up on D0 (active LOW) and enter Deep Sleep
  esp_deep_sleep_enable_gpio_wakeup(1ULL << D0, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
}

// ----------------------------------------------------------------------------
// 11. SETUP & MAIN LOOP
// ----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // Disable Wi-Fi and Bluetooth radios to maximize LiPo battery life
  WiFi.mode(WIFI_OFF);
  btStop();

  // Release backlight pin hold upon waking from Deep Sleep
  gpio_hold_dis((gpio_num_t)TFT_BLK);
  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, LOW);

  pinMode(BTN_PIN, INPUT_PULLUP);

  // Initialize 1.3" ST7789 IPS display (240x240)
  tft.init(240, 240, SPI_MODE3);
  tft.setRotation(2);

  // On cold boot (e.g., plugging in the battery), enter Deep Sleep immediately
  // until the user wakes the keychain by pressing D0
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_GPIO) {
    entrerEnVeilleProfonde();
  }

  // Wait for the wake-up button press to be released before attaching the ISR
  while (digitalRead(BTN_PIN) == LOW) {
    delay(10);
  }
  delay(50);

  boutonAppuye = false;
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), gererAppuiBouton, CHANGE);

  // Pre-draw the menu before turning on the backlight to avoid a white flash
  tft.fillScreen(NOIR);
  menu();
  digitalWrite(TFT_BLK, HIGH);
}

void loop() {
  // Non-blocking state router
  switch (etatActuel) {
    case ETAT_MENU:
      menu();
      break;
    case ETAT_SMILEY:
      dessinerSmiley();
      break;
    case ETAT_HORLOGE:
      horloge();
      break;
    case ETAT_JEU:
      jeu();
      break;
  }
}