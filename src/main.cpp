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
// Note: Passing -1 for CS relies on hardware SPI in SPI_MODE3:
//       SCL -> D8 (GPIO8) | SDA -> D10 (GPIO10)
#define TFT_DC   D4
#define TFT_RST  D3
#define TFT_BLK  D2  // Backlight control pin (held LOW during Deep Sleep)

#define BTN_PIN  D0  // Wake-up capable GPIO pin (configured with INPUT_PULLUP)

// ----------------------------------------------------------------------------
// 2. COLOR PALETTE (16-bit RGB565 format)
// ----------------------------------------------------------------------------
#define NOIR           0x0000  // Black
#define BLEU_CLAIR     0x96BF  // Light Blue (Primary UI accent)
#define JAUNE          0xFFE0  // Yellow
#define ORANGE         0xFD20  // Orange
#define BLANC          0xFFFF  // White
#define FOND_JAUNE     0xF72F  // Exact background yellow matching the sprite sheet
#define FOND_Maron     0xF612  // Warm beige/brown accent

Adafruit_ST7789 tft = Adafruit_ST7789(-1, TFT_DC, TFT_RST);

// ----------------------------------------------------------------------------
// 3. STATE MACHINES & GLOBAL VARIABLES
// ----------------------------------------------------------------------------
enum Etat {
  ETAT_MENU,
  ETAT_SMILEY,
  ETAT_HORLOGE,
  ETAT_JEU
};

enum ModeHorloge {
  AFFICHAGE_HEURE,
  REGLAGE_HEURES,
  REGLAGE_MINUTES
};

enum ModeJeu {
  JEU_PRET,
  JEU_EN_COURS,
  JEU_GAME_OVER
};

void entrerEnVeilleProfonde();

bool boutonAppuye = false;
unsigned long tempsAppuye = 0;

int selection = 0;
Etat etatActuel = ETAT_MENU;
bool majEcran = true;

// 1-bit off-screen buffer in RAM (240x205 saves SPI bandwidth while covering HUD + ground)
GFXcanvas1 canvas(240, 205);

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
float vitesseJeu = 6.5f;
int score = 0;
RTC_DATA_ATTR int meilleurScore = 0; // Persists in RTC memory across Deep Sleep cycles
unsigned long momentGameOver = 0;

// ----------------------------------------------------------------------------
// 5. RELIABLE BUTTON POLLING (Debounced short click & instant long press)
// ----------------------------------------------------------------------------
void lireBouton() {
  static bool etatPrecedent = HIGH;
  static unsigned long debutAppui = 0;
  static bool appuiLongDeclenche = false;

  // During active gameplay, the button is read directly by the jump physics
  if (etatActuel == ETAT_JEU && modeJeu == JEU_EN_COURS) {
    etatPrecedent = digitalRead(BTN_PIN);
    return;
  }

  bool etatActuelBtn = digitalRead(BTN_PIN);

  // Button pressed down
  if (etatPrecedent == HIGH && etatActuelBtn == LOW) {
    debutAppui = millis();
    appuiLongDeclenche = false;
  }

  // Trigger long press immediately once held for 400 ms
  if (etatActuelBtn == LOW && !appuiLongDeclenche && (millis() - debutAppui >= 400)) {
    tempsAppuye = 450;
    boutonAppuye = true;
    appuiLongDeclenche = true;
  }

  // Button released: register short click if not already triggered as a long press
  if (etatPrecedent == LOW && etatActuelBtn == HIGH) {
    unsigned long duree = millis() - debutAppui;
    if (!appuiLongDeclenche && duree >= 40 && duree < 400) {
      tempsAppuye = duree;
      boutonAppuye = true;
    }
  }

  etatPrecedent = etatActuelBtn;
}

// ----------------------------------------------------------------------------
// 6. MAIN MENU
// ----------------------------------------------------------------------------
void menu() {
  const char* options[] = {"Jeu", "Heure", "Smiley", "Eteindre"};

  if (boutonAppuye) {
    if (tempsAppuye < 400) {
      selection = (selection + 1) % 4;
      majEcran = true;
    } else {
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

  if (majEcran && etatActuel == ETAT_MENU) {
    tft.setTextSize(3);
    tft.setTextColor(BLEU_CLAIR, NOIR);
    tft.setCursor(84, 14);
    tft.print("Menu");
    tft.fillRect(20, 44, 200, 3, BLEU_CLAIR);

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
  vitesseJeu = 6.5f;
  score = 0;
  boutonAppuye = false;
}

void lancejeu() {
  static unsigned long derniereFrame = 0;
  static int decalageSol = 0;
  static bool jambeAlternee = false;
  static int compteurPas = 0;

  if (millis() - derniereFrame < 25) {
    return;
  }
  derniereFrame = millis();

  // 1. Zero-latency jump
  if (digitalRead(BTN_PIN) == LOW && auSol) {
    vitesseY = -12.0f;
    auSol = false;
  }

  // 2. Vertical physics
  dinoY += vitesseY;
  vitesseY += 0.90f;
  if (dinoY >= SOL_Y - DINO_H) {
    dinoY = SOL_Y - DINO_H;
    vitesseY = 0;
    auSol = true;
  }

  // 3. Obstacle movement & speed scaling
  cactusX -= vitesseJeu;
  if (cactusX < -CACTUS_L) {
    cactusX = 240 + random(15, 95);
    cactusH = random(22, 34);
    score++;
    if (score > meilleurScore) {
      meilleurScore = score;
    }
    if (vitesseJeu < 12.0f) {
      vitesseJeu += 0.20f;
    }
  }

  // 4. AABB collision detection
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

  canvas.setTextSize(2);
  canvas.setTextColor(1);
  canvas.setCursor(10, 12);
  canvas.printf("HI %03d", meilleurScore);
  canvas.setCursor(155, 12);
  canvas.printf("%03d", score);

  canvas.drawFastHLine(0, SOL_Y, 240, 1);
  decalageSol = (decalageSol + (int)vitesseJeu) % 40;
  for (int x = -decalageSol; x < 240; x += 40) {
    canvas.drawFastHLine(x + 8, SOL_Y + 5, 10, 1);
    canvas.drawFastHLine(x + 26, SOL_Y + 9, 5, 1);
  }

  // Procedural T-Rex sprite
  canvas.fillRect(DINO_X + 8, dY, 14, 10, 1);
  canvas.fillRect(DINO_X + 16, dY + 2, 2, 2, 0);
  canvas.fillRect(DINO_X + 4, dY + 9, 13, 11, 1);
  canvas.fillRect(DINO_X, dY + 11, 4, 6, 1);
  canvas.fillRect(DINO_X + 17, dY + 12, 4, 2, 1);

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

  // 6. Push 240x205 frame buffer to ST7789 over fast SPI
  tft.drawBitmap(0, 0, canvas.getBuffer(), 240, 205, BLEU_CLAIR, NOIR);
}

void jeu() {
  if (modeJeu == JEU_PRET) {
    if (majEcran) {
      tft.fillScreen(NOIR);
      tft.setTextSize(3);
      tft.setTextColor(BLEU_CLAIR, NOIR);
      tft.setCursor(48, 35);
      tft.print("DINO RUN");

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
  else if (modeJeu == JEU_EN_COURS) {
    lancejeu();
  } 
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

  time_t maintenant;
  time(&maintenant);
  struct tm temps;
  localtime_r(&maintenant, &temps);

  if (boutonAppuye) {
    if (mode == AFFICHAGE_HEURE) {
      if (tempsAppuye >= 400) {
        etatActuel = ETAT_MENU;
        tft.fillScreen(NOIR);
        majEcran = true;
        boutonAppuye = false;
        return;
      } else {
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
        mode = REGLAGE_MINUTES;
        majEcran = true;
      }
    } else if (mode == REGLAGE_MINUTES) {
      if (tempsAppuye < 400) {
        minutesReglage = (minutesReglage + 1) % 60;
        majEcran = true;
      } else {
        struct tm nouveauTemps = {0};
        nouveauTemps.tm_year = 2026 - 1900;
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

  if (mode == AFFICHAGE_HEURE && (millis() - dernierRafraichissement >= 1000)) {
    majEcran = true;
  }

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

    if (mode == REGLAGE_HEURES) {
      tft.setTextColor(NOIR, BLANC);
    } else {
      tft.setTextColor(BLANC, NOIR);
    }
    tft.printf("%02d", hAffichage);

    tft.setTextColor(BLEU_CLAIR, NOIR);
    tft.print(":");

    if (mode == REGLAGE_MINUTES) {
      tft.setTextColor(NOIR, BLANC);
    } else {
      tft.setTextColor(BLANC, NOIR);
    }
    tft.printf("%02d", mAffichage);

    tft.setTextSize(2);
    tft.setCursor(108, 140);
    if (mode == AFFICHAGE_HEURE) {
      tft.setTextColor(BLEU_CLAIR, NOIR);
      tft.printf("%02d", temps.tm_sec);
    } else {
      tft.setTextColor(NOIR, NOIR);
      tft.print("  ");
    }

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

  if (boutonAppuye) {
    if (tempsAppuye >= 400) {
      etatActuel = ETAT_MENU;
      tft.fillScreen(NOIR);
      majEcran = true;
      boutonAppuye = false;
      return;
    } else {
      indexActuel = (indexActuel + 1) % NB_EXPRESSIONS;
      afficherExpression(indexActuel);
      dernierChangement = millis();
      dureeMaintien = 4000;
    }
    boutonAppuye = false;
  }

  if (majEcran) {
    tft.fillScreen(FOND_JAUNE);
    indexActuel = 0;
    afficherExpression(indexActuel);
    dernierChangement = millis();
    dureeMaintien = 3500;
    majEcran = false;
  }

  unsigned long maintenant = millis();
  if (maintenant - dernierChangement >= dureeMaintien) {
    if (indexActuel == 0) {
      indexActuel = indexAuto;
      indexAuto++;
      if (indexAuto > 4) indexAuto = 1;
      dureeMaintien = 900;
    } else {
      indexActuel = 0;
      dureeMaintien = 3500;
    }
    afficherExpression(indexActuel);
    dernierChangement = maintenant;
  }
}

// ----------------------------------------------------------------------------
// 10. POWER MANAGEMENT (DEEP SLEEP)
// ----------------------------------------------------------------------------
void entrerEnVeilleProfonde() {
  digitalWrite(TFT_BLK, LOW);
  gpio_hold_en((gpio_num_t)TFT_BLK);
  gpio_deep_sleep_hold_en();

  tft.enableDisplay(false);
  tft.enableSleep(true);

  while (digitalRead(BTN_PIN) == LOW) {
    delay(10);
  }
  delay(50);

  esp_deep_sleep_enable_gpio_wakeup(1ULL << D0, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
}

// ----------------------------------------------------------------------------
// 11. SETUP & MAIN LOOP
// ----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_OFF);
  btStop();

  gpio_hold_dis((gpio_num_t)TFT_BLK);
  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, LOW);

  pinMode(BTN_PIN, INPUT_PULLUP);

  // Initialize 1.3" ST7789 IPS display (240x240) at 40 MHz SPI
  tft.init(240, 240, SPI_MODE3);
  tft.setSPISpeed(40000000);
  tft.setRotation(2);

  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_GPIO) {
    entrerEnVeilleProfonde();
  }

  while (digitalRead(BTN_PIN) == LOW) {
    delay(10);
  }
  delay(50);

  boutonAppuye = false;

  tft.fillScreen(NOIR);
  menu();
  digitalWrite(TFT_BLK, HIGH);
}

void loop() {
  lireBouton();

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