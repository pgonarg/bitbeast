/*
  BitBeast -- a virtual-monster raising pet for Arduino UNO + 128x64 SSD1306 OLED.

  Hardware layout and spirit taken from Tamaguino by Alojz Jakob
  (https://github.com/alojzjakob/Tamaguino, GPLv3); game logic, art and
  creatures are new. Licensed GPLv3, like Tamaguino.

  Buttons:  A (D9) = next icon    B (D8) = confirm    C (D7) = back
  See README.md for wiring, controls and the care guide.
*/

#include <Wire.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
// Bundled copy of the Adafruit SSD1306 driver with its unused logo splash removed
// (saves ~1.5 KB of flash). Needs the Adafruit GFX library installed as usual.
#include "src/Adafruit_SSD1306/Adafruit_SSD1306.h"

#include "config.h"
#include "types.h"
#include "sprites.h"
#include "species.h"

#define C_ON  SSD1306_WHITE
#define C_OFF SSD1306_BLACK
#define C_INV SSD1306_INVERSE

#define BTN_A 1
#define BTN_B 2
#define BTN_C 4

// Direct port access for the UNO's ATmega328P (pins D0-D7 = PORTD, D8-D13 = PORTB,
// A0-A5 = PORTC). Pin numbers are constants, so each use compiles to 1-2 instructions
// instead of a call into digitalRead()/digitalWrite().
#define PIN_PORT(p)    ((p) < 8 ? &PORTD : (p) < 14 ? &PORTB : &PORTC)
#define PIN_DDR(p)     ((p) < 8 ? &DDRD  : (p) < 14 ? &DDRB  : &DDRC)
#define PIN_IN(p)      ((p) < 8 ? &PIND  : (p) < 14 ? &PINB  : &PINC)
#define PIN_MASK(p)    ((uint8_t)(1 << ((p) < 8 ? (p) : (p) < 14 ? (p) - 8 : (p) - 14)))
#define FAST_READ(p)   ((*PIN_IN(p) & PIN_MASK(p)) != 0)
#define FAST_HIGH(p)   (*PIN_PORT(p) |= PIN_MASK(p))
#define FAST_LOW(p)    (*PIN_PORT(p) &= (uint8_t)~PIN_MASK(p))
#define FAST_WRITE(p, v) do { if (v) FAST_HIGH(p); else FAST_LOW(p); } while (0)
#define FAST_OUTPUT(p) (*PIN_DDR(p) |= PIN_MASK(p))
#define FAST_INPUT(p)  (*PIN_DDR(p) &= (uint8_t)~PIN_MASK(p))
#define FAST_TOGGLE(p) (*PIN_IN(p) = PIN_MASK(p))     // writing PINx toggles on the 328P
static_assert(PIN_BTN_A < 20 && PIN_BTN_B < 20 && PIN_BTN_C < 20 && PIN_LED < 20 && PIN_BUZZER < 20 &&
              PIN_ACOM_OUT < 20,
              "pins must be UNO pins D0-D13 / A0-A5 (14-19)");

#define SAVE_MAGIC 0xB17C
#define SLOT_SIZE  64
#define SLOT_COUNT ((E2END + 1) / SLOT_SIZE)
static_assert(sizeof(SaveBlock) <= SLOT_SIZE, "SaveBlock does not fit an EEPROM slot");

Adafruit_SSD1306 display(128, 64, &Wire, -1);

// ---------------------------------------------------------------- state
PetState pet;
Species  sp;                       // RAM copy of the current species row
uint8_t  evolvePending = SP_NONE;

const uint16_t SPEEDS[4] = {1, 10, 60, 600};   // game-minutes per real minute

const uint8_t* const ICONS[8] PROGMEM = {
  SPR_I_STATS, SPR_I_FEED, SPR_I_TRAIN, SPR_I_BATTLE,
  SPR_I_FLUSH, SPR_I_LIGHT, SPR_I_HEAL, SPR_I_SETUP
};

// UI
uint8_t  mode = M_HOME;
unsigned long modeT, phaseT;
int8_t   sel = -1;
uint8_t  sub, phase;
bool     phaseFlag;
const __FlashStringHelper* msgText;

// wandering pet on the home screen
int16_t  petX = 48;
int16_t  petMaxX = 62;             // home screen keeps the right side free for poop
bool     petLeft, petBob, petAct;   // petLeft: facing / walking left
unsigned long petT;

// mini-game / battle / animation scratch
uint8_t  gRound, gHits, gMask, zoneX, zoneW;
int16_t  gCursor;
int8_t   gDir;
uint8_t  enemyId, myHp, enHp, hitChance;
uint8_t  myMouth, enMouth;          // attack launch points (see mouthOf)
bool     shotHit, battleWon;
bool     linkFight;                 // this battle's result was settled over the A-Com link
uint8_t  evoFrom, evoTo, eatType;
uint8_t  clockH, clockM;

// system
unsigned long lastMinuteMs, lastFrame, lastSave, lastInput, lastCallBeep;
bool     callWas, callAck = true, screenOff, dimmed;
uint16_t saveSeq;
uint8_t  saveSlot;

// buttons
uint8_t  btnStable, btnLastRaw, evPress, evRepeat;
unsigned long btnChange, btnNextRepeat;

// forward declarations (the Arduino IDE generates these itself; PlatformIO does not)
void startTrain();
void startBattle();
void beginBattle(uint8_t hc);
void startEvolve(uint8_t to);
void saveNow();

// ---------------------------------------------------------------- sound
// Melodies: {freq, ms} pairs, freq 0 = rest, ms 0 = end.
const uint16_t MEL_CLICK[]  PROGMEM = {2200, 15, 0, 0};
const uint16_t MEL_CALL[]   PROGMEM = {1800, 90, 0, 60, 1800, 90, 0, 60, 1800, 90, 0, 0};
const uint16_t MEL_HAPPY[]  PROGMEM = {1047, 80, 1319, 80, 1568, 140, 0, 0};
const uint16_t MEL_SAD[]    PROGMEM = {440, 150, 349, 150, 262, 260, 0, 0};
const uint16_t MEL_EVOLVE[] PROGMEM = {523, 90, 659, 90, 784, 90, 1047, 90, 784, 90, 1047, 300, 0, 0};
const uint16_t MEL_WIN[]    PROGMEM = {784, 100, 988, 100, 1175, 100, 1568, 300, 0, 0};
const uint16_t MEL_LOSE[]   PROGMEM = {392, 200, 330, 200, 262, 400, 0, 0};
const uint16_t MEL_DEATH[]  PROGMEM = {523, 300, 494, 300, 466, 300, 440, 700, 0, 0};
const uint16_t MEL_START[]  PROGMEM = {523, 120, 659, 120, 784, 120, 1047, 260, 0, 0};
const uint16_t MEL_SHOOT[]  PROGMEM = {1200, 40, 900, 40, 0, 0};
const uint16_t MEL_HIT[]    PROGMEM = {180, 70, 0, 0};
const uint16_t MEL_EAT[]    PROGMEM = {320, 40, 0, 0};
const uint16_t MEL_POOP[]   PROGMEM = {150, 90, 110, 90, 0, 0};

const uint16_t* melPtr = NULL;
unsigned long noteEnd;

inline bool flag(uint8_t f) { return (pet.flags & f) != 0; }
inline void setFlag(uint8_t f, bool on) { if (on) pet.flags |= f; else pet.flags &= ~f; }

// Tiny 16-bit xorshift random generator: plenty for a pet, and ~300 bytes smaller
// than Arduino's random(). rnd(n) = 0..n-1, rndRange(a, b) = a..b-1 (like random(a, b)).
uint16_t rngState = 0xACE1;
void rndSeed(uint32_t s) {
  rngState = (uint16_t)(s ^ (s >> 16));
  if (!rngState) rngState = 0xACE1;
}
uint16_t rnd(uint16_t n) {
  uint16_t x = rngState;
  x ^= x << 7;
  x ^= x >> 9;
  x ^= x << 8;
  rngState = x;
  return n ? x % n : 0;
}
int16_t rndRange(int16_t a, int16_t b) { return a + (int16_t)rnd(b - a); }

void play(const uint16_t* m) {
  if (flag(F_SOUNDOFF)) return;
  melPtr = m;
  noteEnd = millis();
}

void updateSound() {
  if (!melPtr) return;
  unsigned long now = millis();
  if ((long)(now - noteEnd) < 0) return;
  uint16_t f = pgm_read_word(melPtr);
  uint16_t d = pgm_read_word(melPtr + 1);
  if (d == 0) { melPtr = NULL; noTone(PIN_BUZZER); return; }
  if (f) tone(PIN_BUZZER, f, d); else noTone(PIN_BUZZER);
  noteEnd = now + d;
  melPtr += 2;
}

void waitMs(unsigned long ms) {
  unsigned long s = millis();
  while (millis() - s < ms) updateSound();
}

// ---------------------------------------------------------------- helpers
bool blink() { return (millis() / 400) & 1; }
bool blinkFast() { return (millis() / 150) & 1; }

void loadSpecies() { memcpy_P(&sp, &SPECIES[pet.species], sizeof(Species)); }

void setMode(uint8_t m) {
  mode = m;
  modeT = phaseT = millis();
  sub = 0;
  phase = 0;
  phaseFlag = false;
}

unsigned long modeElapsed() { return millis() - modeT; }

void addWeight(int8_t d) {
  int16_t w = (int16_t)pet.weight + d;
  if (w < 1) w = 1;
  if (w > 99) w = 99;
  pet.weight = w;
}

bool anyCall() {
  if (flag(F_DEAD) || sp.stage == ST_EGG) return false;
  if (flag(F_ASLEEP)) return !flag(F_LIGHTSOFF);
  return pet.hunger == 0 || pet.strength == 0;
}

void wakeScreen() {
  if (screenOff) { display.ssd1306_command(SSD1306_DISPLAYON); screenOff = false; }
  if (dimmed) { display.dim(false); dimmed = false; }
  lastInput = millis();
}

// ---------------------------------------------------------------- drawing
// Draws a 1-bit PROGMEM bitmap (Adafruit format) with integer scaling and optional mirroring.
void drawSprite(const uint8_t* bmp, int16_t x, int16_t y, uint8_t w, uint8_t h,
                uint8_t scale, bool flip, uint16_t color) {
  uint8_t bpr = (w + 7) / 8;
  for (uint8_t j = 0; j < h; j++) {
    for (uint8_t i = 0; i < w; i++) {
      uint8_t b = pgm_read_byte(bmp + j * bpr + (i >> 3));
      if (b & (0x80 >> (i & 7))) {
        int16_t px = flip ? (w - 1 - i) : i;
        if (scale == 1) display.drawPixel(x + px, y + j, color);
        else display.fillRect(x + px * scale, y + j * scale, scale, scale, color);
      }
    }
  }
}

// Creatures are 16x16 drawn at 2x (32x32 on screen). The art faces LEFT;
// faceRight mirrors it.
void drawMon(uint8_t id, uint8_t frame, int16_t x, int16_t y, bool faceRight) {
  const uint8_t* p = (const uint8_t*)pgm_read_word(&SPECIES[id].spr[frame & 1]);
  drawSprite(p, x, y, 16, 16, 2, faceRight, C_ON);
}

bool sprPixel(const uint8_t* bmp, uint8_t x, uint8_t y) {
  return pgm_read_byte(bmp + y * 2 + (x >> 3)) & (0x80 >> (x & 7));
}

// Where a creature's attack comes from, packed as (x << 4) | y in sprite pixels (art faces
// left). It's the leading (leftmost) column of the pixels that change between the idle and
// action frames: open mouth, breath, raised claw... If both frames are the same, the snout
// (leftmost lit column of the action frame) is used instead.
uint8_t mouthOf(uint8_t id) {
  const uint8_t* f0 = (const uint8_t*)pgm_read_word(&SPECIES[id].spr[0]);
  const uint8_t* f1 = (const uint8_t*)pgm_read_word(&SPECIES[id].spr[1]);
  for (uint8_t pass = 0; pass < 2; pass++) {
    for (uint8_t x = 0; x < 16; x++) {
      uint8_t n = 0, sumY = 0;
      for (uint8_t y = 0; y < 16; y++) {
        bool hit = pass == 0 ? (sprPixel(f0, x, y) != sprPixel(f1, x, y)) : sprPixel(f1, x, y);
        if (hit) { n++; sumY += y; }
      }
      if (n) return (x << 4) | (sumY / n);
    }
  }
  return 8;
}

// Screen position of that point for a creature drawn by drawMon() at (x, y).
int16_t mouthScreenX(uint8_t m, int16_t x, bool faceRight) {
  uint8_t mx = m >> 4;
  return x + (faceRight ? (15 - mx) * 2 + 1 : mx * 2);
}
int16_t mouthScreenY(uint8_t m, int16_t y) { return y + (m & 15) * 2 + 1; }

// The SHOT art points left; it is mirrored when flying right. (x, y) is the shot's back end:
// the shot sits just ahead of that point, vertically centred on it.
void drawShot(int16_t x, int16_t y, bool right) {
  drawSprite(SPR_SHOT, right ? x - 1 : x - 7, y - 4, 8, 8, 1, right, C_ON);
}

void icon(const uint8_t* bmp, int16_t x, int16_t y, uint8_t scale) {
  drawSprite(bmp, x, y, 8, 8, scale, false, C_ON);
}

void textAt(int16_t x, int16_t y, uint8_t size, const __FlashStringHelper* s) {
  display.setTextSize(size);
  display.setCursor(x, y);
  display.print(s);
}

void textCenter(int16_t y, uint8_t size, const __FlashStringHelper* s) {
  int16_t w = strlen_P((PGM_P)s) * 6 * size;
  textAt((128 - w) / 2, y, size, s);
}

void textCenterRam(int16_t y, uint8_t size, const char* s) {
  int16_t w = strlen(s) * 6 * size;
  display.setTextSize(size);
  display.setCursor((128 - w) / 2, y);
  display.print(s);
}

// A label at the left and a number at the right (stats pages).
void statLine(int16_t y, const __FlashStringHelper* label, uint16_t v, char unit) {
  textAt(10, y, 1, label);
  display.setCursor(84, y);
  display.print(v);
  if (unit) display.print(unit);
}

void option(int16_t x, int16_t y, const __FlashStringHelper* s, bool selected) {
  int16_t w = strlen_P((PGM_P)s) * 6 + 4;
  if (selected) {
    display.fillRect(x - 2, y - 2, w, 11, C_ON);
    display.setTextColor(C_OFF);
  }
  textAt(x, y, 1, s);
  display.setTextColor(C_ON);
}

void drawHearts(uint8_t n, int16_t y) {
  for (uint8_t i = 0; i < 4; i++)
    icon(i < n ? SPR_HEART_F : SPR_HEART_E, 20 + i * 24, y, 2);
}

void drawTime(uint8_t h, uint8_t m, int16_t y, bool colon) {
  display.setTextSize(3);
  display.setCursor(19, y);
  if (h < 10) display.print('0');
  display.print(h);
  display.print(colon ? ':' : ' ');
  if (m < 10) display.print('0');
  display.print(m);
}

void drawZzz(int16_t x, int16_t y) {
  bool b = blink();
  display.setTextSize(1);
  display.setCursor(x, y + (b ? 0 : 3));
  display.print('z');
  display.setCursor(x + 6, y - 6 + (b ? 3 : 0));
  display.print('Z');
}

void drawIconBar() {
  for (uint8_t i = 0; i < 8; i++) {
    int16_t x = i * 16 + 4;          // 8 slots of 16 px, all in the top band
    int16_t y = 4;
    const uint8_t* ic = (const uint8_t*)pgm_read_word(&ICONS[i]);
    if (sel == (int8_t)i) {
      display.fillRect(x - 3, y - 3, 14, 14, C_ON);
      drawSprite(ic, x, y, 8, 8, 1, false, C_OFF);
    } else {
      drawSprite(ic, x, y, 8, 8, 1, false, C_ON);
    }
  }
}

void drawHome() {
  drawIconBar();
  bool asleep = flag(F_ASLEEP);
  if (sp.stage == ST_EGG) {
    int16_t wob = 0;
    if (pet.stageMin + 1 >= sp.evolveMin) wob = blinkFast() ? 2 : -2;
    drawMon(pet.species, 0, 48 + wob, 26, false);
  } else if (flag(F_LIGHTSOFF)) {
    if (asleep) drawZzz(petX + 30, 32);
  } else {
    int16_t y = (petBob && !asleep) ? 26 : 28;
    drawMon(pet.species, petAct ? 1 : 0, petX, y, !petLeft);
    if (asleep) drawZzz(petX + 30, 30);
    for (uint8_t i = 0; i < pet.poops; i++)
      icon(blink() ? SPR_POOP1 : SPR_POOP0, 96 + (i & 1) * 16, 46 - (i >> 1) * 16, 2);
    if (flag(F_SICK | F_INJURED) && blink()) icon(SPR_SKULL, petX + 32, 20, 1);
  }
  if (anyCall() && blink()) {
    display.setTextSize(1);
    display.setCursor(122, 18);
    display.print('!');
  }
}

void updateWander() {
  if (millis() - petT < 600) return;
  petT = millis();
  petBob = !petBob;
  petAct = false;
  if (flag(F_ASLEEP) || sp.stage == ST_EGG) return;
  uint8_t r = rnd(10);
  if (r < 2) petLeft = !petLeft;
  else if (r < 7) petX += petLeft ? -4 : 4;
  else if (r == 9) petAct = true;
  if (petX < 0)  { petX = 0;  petLeft = false; }
  if (petX > petMaxX) { petX = petMaxX; petLeft = true; }
}

// ---------------------------------------------------------------- save / load
uint8_t crc8(const uint8_t* p, uint8_t n) {
  uint8_t c = 0x5A;
  while (n--) { c ^= *p++; c = (c << 1) | (c >> 7); }
  return c;
}

void saveNow() {
  SaveBlock b;
  b.magic = SAVE_MAGIC;
  b.seq = ++saveSeq;
  b.s = pet;
  b.crc = crc8((const uint8_t*)&b.s, sizeof(PetState));
  saveSlot = (saveSlot + 1) % SLOT_COUNT;
  EEPROM.put(saveSlot * SLOT_SIZE, b);     // put() only rewrites bytes that changed
  lastSave = millis();
}

bool loadGame() {
  SaveBlock b;
  bool found = false;
  for (uint8_t i = 0; i < SLOT_COUNT; i++) {
    EEPROM.get(i * SLOT_SIZE, b);
    if (b.magic != SAVE_MAGIC) continue;
    if (crc8((const uint8_t*)&b.s, sizeof(PetState)) != b.crc) continue;
    if (b.s.species >= SP_COUNT || b.s.speedIdx > 3 || b.s.clockMin >= 1440) continue;
    if (!found || (int16_t)(b.seq - saveSeq) > 0) {
      found = true;
      saveSeq = b.seq;
      saveSlot = i;
      pet = b.s;
    }
  }
  return found;
}

// ---------------------------------------------------------------- life cycle
void newEgg() {
  uint8_t  gen = pet.generation < 255 ? pet.generation + 1 : 255;
  uint8_t  spd = pet.speedIdx;
  uint8_t  snd = pet.flags & F_SOUNDOFF;
  uint16_t clk = pet.clockMin;
  memset(&pet, 0, sizeof(pet));
  pet.generation = gen;
  pet.speedIdx = spd;
  pet.flags = snd;
  pet.clockMin = clk;
  pet.species = 0;                  // species 0 is the starting egg
  loadSpecies();
  pet.weight = sp.baseWeight;
  evolvePending = SP_NONE;
  sel = -1;
  setMode(M_HOME);
  saveNow();
}

void die() {
  setFlag(F_DEAD, true);
  setFlag(F_SICK | F_INJURED | F_ASLEEP, false);
  evolvePending = SP_NONE;
  wakeScreen();
  play(MEL_DEATH);
  setMode(M_DEAD);
  saveNow();
}

void makeSick() {
  setFlag(F_SICK, true);
  if (pet.injuries < 255) pet.injuries++;
  uint8_t m = rndRange(1, 3);
  if (pet.medsNeeded < m) pet.medsNeeded = m;
}

void injure() {
  setFlag(F_INJURED, true);
  if (pet.injuries < 255) pet.injuries++;
  if (pet.medsNeeded < 1) pet.medsNeeded = 1;
}

// Walks EVO_RULES (species.h) in order; the first matching rule wins.
// SP_NONE = no rule matches yet (the pet keeps checking every minute).
uint8_t chooseEvolution() {
  for (uint8_t i = 0; i < EVO_RULE_COUNT; i++) {
    EvoRule r;
    memcpy_P(&r, &EVO_RULES[i], sizeof(EvoRule));
    if (r.from != pet.species) continue;
    if (pet.careMistakes < r.minMistakes || pet.careMistakes > r.maxMistakes) continue;
    if (pet.trainings < r.minTrain || pet.trainings > r.maxTrain) continue;
    if (pet.stageBattles < r.minBattles) continue;
    if (r.minWinPct && (pet.stageBattles == 0 ||
        (uint32_t)pet.stageWins * 100 < (uint32_t)pet.stageBattles * r.minWinPct)) continue;
    if (pet.weight < r.minWeight || pet.weight > r.maxWeight) continue;
    if (r.to >= SP_COUNT) continue;
    return r.to;
  }
  return SP_NONE;
}

void applyEvolution(uint8_t id) {
  bool hatch = sp.stage == ST_EGG;
  pet.species = id;
  loadSpecies();
  pet.weight = sp.baseWeight;
  pet.careMistakes = pet.trainings = pet.overfeeds = 0;
  pet.stageBattles = pet.stageWins = 0;
  pet.stageMin = 0;
  pet.hungerTmr = pet.strengthTmr = pet.poopTmr = 0;
  pet.hungerCall = pet.strengthCall = pet.lightsCall = 0;
  if (hatch) {
    pet.hunger = 2;
    pet.strength = 2;
    pet.age = 0;
    pet.dayMin = 0;
  }
  petX = 48;
  petLeft = false;
  saveNow();
}

bool inSleepWindow() {
  if (sp.sleepHour == NOSLEEP) return false;
  uint8_t h = pet.clockMin / 60;
  if (sp.sleepHour > sp.wakeHour) return h >= sp.sleepHour || h < sp.wakeHour;
  return h >= sp.sleepHour && h < sp.wakeHour;
}

// A call ignored for CALL_TIMEOUT_MIN minutes costs one care mistake.
void tickCall(uint8_t &ctr, bool active) {
  if (!active) { ctr = 0; return; }
  if (ctr < 255) ctr++;
  if (ctr == CALL_TIMEOUT_MIN && pet.careMistakes < 255) pet.careMistakes++;
}

// Called once per game-minute.
void gameMinute() {
  pet.clockMin = (pet.clockMin + 1) % 1440;
  if (flag(F_DEAD)) return;

  if (++pet.dayMin >= 1440) {
    pet.dayMin = 0;
    if (pet.age < 255) pet.age++;
  }
  if (pet.stageMin < 65535) pet.stageMin++;

  bool evolving = mode == M_EVOLVE || evolvePending != SP_NONE;
  if (sp.stage == ST_EGG) {
    if (pet.stageMin >= sp.evolveMin && !evolving) evolvePending = chooseEvolution();
    return;
  }

  // sleep / wake
  bool shouldSleep = inSleepWindow();
  if (shouldSleep && !flag(F_ASLEEP)) {
    setFlag(F_ASLEEP, true);
  } else if (!shouldSleep && flag(F_ASLEEP)) {
    setFlag(F_ASLEEP, false);
    setFlag(F_LIGHTSOFF, false);        // morning: lights come back on
  }
  bool asleep = flag(F_ASLEEP);

  // needs drain only while awake
  if (!asleep) {
    if (++pet.hungerTmr >= sp.hungerMin) {
      pet.hungerTmr = 0;
      if (pet.hunger) pet.hunger--;
      setFlag(F_OVERFED, false);
    }
    if (++pet.strengthTmr >= sp.strengthMin) {
      pet.strengthTmr = 0;
      if (pet.strength) pet.strength--;
    }
    if (++pet.poopTmr >= sp.poopMin) {
      pet.poopTmr = 0;
      if (pet.poops < 4) { pet.poops++; play(MEL_POOP); }
      if (pet.poops >= 4 && !flag(F_SICK)) makeSick();
    }
  }

  tickCall(pet.hungerCall,   !asleep && pet.hunger == 0);
  tickCall(pet.strengthCall, !asleep && pet.strength == 0);
  tickCall(pet.lightsCall,   asleep && !flag(F_LIGHTSOFF));

  // ways to die
  if (!asleep && pet.hunger == 0) {
    if (++pet.starveMin >= STARVE_DEATH_MIN) { die(); return; }
  } else if (pet.hunger > 0) {
    pet.starveMin = 0;
  }
  if (flag(F_SICK | F_INJURED)) {
    if (++pet.sickMin >= SICK_DEATH_MIN) { die(); return; }
  } else {
    pet.sickMin = 0;
  }
  if (pet.injuries >= MAX_INJURIES) { die(); return; }
  if (sp.lifespanDays && pet.age >= sp.lifespanDays) { die(); return; }

  // evolution
  if (sp.evolveMin && pet.stageMin >= sp.evolveMin && !asleep &&
      !flag(F_SICK | F_INJURED) && !evolving) {
    evolvePending = chooseEvolution();
  }
}

// ---------------------------------------------------------------- input
uint8_t readRaw() {
  uint8_t b = 0;
#if BUTTONS_ACTIVE_LOW
  if (!FAST_READ(PIN_BTN_A)) b |= BTN_A;
  if (!FAST_READ(PIN_BTN_B)) b |= BTN_B;
  if (!FAST_READ(PIN_BTN_C)) b |= BTN_C;
#else
  if (FAST_READ(PIN_BTN_A)) b |= BTN_A;
  if (FAST_READ(PIN_BTN_B)) b |= BTN_B;
  if (FAST_READ(PIN_BTN_C)) b |= BTN_C;
#endif
  return b;
}

void pollButtons() {
  uint8_t raw = readRaw();
  unsigned long now = millis();
  if (raw != btnLastRaw) { btnLastRaw = raw; btnChange = now; }
  if (now - btnChange >= DEBOUNCE_MS && raw != btnStable) {
    uint8_t pressed = raw & ~btnStable;
    btnStable = raw;
    if (pressed) {
      bool wasOff = screenOff;
      wakeScreen();
      callAck = true;                       // any press silences repeat alarms
      btnNextRepeat = now + 450;
      if (!wasOff) {                        // the press that wakes the screen is swallowed
        evPress |= pressed;
        play(MEL_CLICK);
      }
    }
  }
  if (btnStable && (long)(now - btnNextRepeat) >= 0) {
    evRepeat |= btnStable;
    btnNextRepeat = now + 110;
  }
}

void checkCallAlarm() {
  bool c = anyCall();
  unsigned long now = millis();
  if (c && !callWas) {
    callAck = false;
    lastCallBeep = now;
    play(MEL_CALL);
    wakeScreen();
  } else if (c && !callAck && now - lastCallBeep > 60000UL) {
    lastCallBeep = now;
    play(MEL_CALL);
  }
  callWas = c;
  FAST_WRITE(PIN_LED, c && blink());
}

// ---------------------------------------------------------------- simple modes
void showMsg(const __FlashStringHelper* s) { msgText = s; setMode(M_MSG); }

void startHappy() { setMode(M_HAPPY); play(MEL_HAPPY); }
void startSad()   { setMode(M_SAD);   play(MEL_SAD); }

void modeMsg(uint8_t ev) {
  if (ev || modeElapsed() > 1500) { setMode(M_HOME); return; }
  textCenter(36, 1, msgText);
}

void modeRefuse(uint8_t ev) {
  unsigned long t = modeElapsed();
  if (t >= 1200) { setMode(M_HOME); return; }
  drawMon(pet.species, 0, 48, 24, (t / 150) & 1);
}

void modeHappy(uint8_t ev) {
  unsigned long t = modeElapsed();
  if (t >= 1500) { setMode(M_HOME); return; }
  bool up = (t / 250) & 1;
  drawMon(pet.species, 1, 48, up ? 18 : 26, false);
  if (up) { icon(SPR_SUN, 20, 22, 1); icon(SPR_SUN, 100, 22, 1); }
  else    { icon(SPR_SUN, 14, 40, 1); icon(SPR_SUN, 106, 40, 1); }
}

void modeSad(uint8_t ev) {
  unsigned long t = modeElapsed();
  if (t >= 1500) { setMode(M_HOME); return; }
  drawMon(pet.species, 0, 48, 31, false);
  icon(SPR_CLOUD, 56, ((t / 250) & 1) ? 15 : 17, 2);
}

// ---------------------------------------------------------------- home
void doAction(uint8_t a) {
  bool egg = sp.stage == ST_EGG;
  bool asleep = flag(F_ASLEEP);
  bool ill = flag(F_SICK | F_INJURED);
  switch (a) {
    case 0: setMode(M_STATS); break;
    case 1:
      if (egg) showMsg(F("..."));
      else if (asleep) showMsg(F("Zzz..."));
      else setMode(M_FEED);
      break;
    case 2:
      if (egg) showMsg(F("..."));
      else if (asleep) showMsg(F("Zzz..."));
      else if (ill) setMode(M_REFUSE);
      else startTrain();
      break;
    case 3:
      if (sp.stage < ST_ROOKIE) showMsg(F("Too young!"));
      else if (asleep) showMsg(F("Zzz..."));
      else if (ill) setMode(M_REFUSE);
      else if (pet.hunger == 0) showMsg(F("Too hungry!"));
#if ACOM_ENABLED
      else setMode(M_LINK);
#else
      else startBattle();
#endif
      break;
    case 4:
      if (egg) showMsg(F("..."));
      else setMode(M_FLUSH);
      break;
    case 5:
      setMode(M_LIGHTS);
      sub = flag(F_LIGHTSOFF) ? 1 : 0;
      break;
    case 6:
      if (egg) showMsg(F("..."));
      else if (!ill) setMode(M_REFUSE);
      else setMode(M_HEAL);
      break;
    case 7: setMode(M_SETUP); break;
  }
}

void modeHome(uint8_t ev) {
  if (evolvePending != SP_NONE) { startEvolve(evolvePending); return; }
  if (ev & BTN_A) sel = (sel + 1) & 7;
  if (ev & BTN_C) sel = -1;
  if (ev & BTN_B) {
    if (sel < 0) setMode(M_CLOCK);
    else doAction(sel);
    return;
  }
  if (millis() - lastInput > 30000UL) sel = -1;
  updateWander();
  drawHome();
}

// ---------------------------------------------------------------- stats
void modeStats(uint8_t ev) {
  if (ev & (BTN_A | BTN_B)) {
    if (++sub > 5) { setMode(M_HOME); return; }
  }
  if (ev & BTN_C) { setMode(M_HOME); return; }

  display.setTextSize(1);
  switch (sub) {
    case 0: {
      // big letters fit 10 characters; an 11-letter name drops to normal size
      bool big = strlen(sp.name) <= 10;
      display.setTextSize(big ? 2 : 1);
      display.setCursor(0, big ? 1 : 4);
      display.print(sp.name);
      char buf[10];
      strcpy_P(buf, STAGE_NAMES[sp.stage]);
      display.setTextSize(1);
      display.setCursor(0, 20);
      display.print(buf);
      display.setCursor(0, 34);
      display.print(F("Age "));
      display.print(pet.age);
      display.print('d');
      display.setCursor(0, 48);
      display.print(F("Wt  "));
      display.print(pet.weight);
      display.print('g');
      drawMon(pet.species, 0, 92, 24, false);
    } break;
    case 1: textCenter(4, 1, F("HUNGER"));   drawHearts(pet.hunger, 32);   break;
    case 2: textCenter(4, 1, F("STRENGTH")); drawHearts(pet.strength, 32); break;
    case 3: textCenter(4, 1, F("EFFORT"));   drawHearts(pet.effort, 32);   break;
    case 4: {
      textCenter(4, 1, F("BATTLES"));
      statLine(22, F("Wins"), pet.totalWins, 0);
      statLine(34, F("Losses"), pet.totalBattles - pet.totalWins, 0);
      uint16_t rate = pet.totalBattles ? (uint32_t)pet.totalWins * 100 / pet.totalBattles : 0;
      statLine(46, F("Win rate"), rate, '%');
    } break;
    case 5:
      textCenter(4, 1, F("CARE"));
      statLine(18, F("Mistakes"), pet.careMistakes, 0);
      statLine(27, F("Training"), pet.trainings, 0);
      statLine(36, F("Injuries"), pet.injuries, 0);
      statLine(45, F("Overfeed"), pet.overfeeds, 0);
      statLine(54, F("Generation"), pet.generation, 0);
      break;
  }
  if (sub < 5) {
    display.setTextSize(1);
    display.setCursor(110, 56);
    display.print(sub + 1);
    display.print(F("/6"));
  }
}

// ---------------------------------------------------------------- feeding
void feed(uint8_t t) {
  if (t == 0) {
    if (pet.hunger < 4) {
      pet.hunger++;
      addWeight(1);
    } else if (!flag(F_OVERFED)) {
      setFlag(F_OVERFED, true);
      if (pet.overfeeds < 255) pet.overfeeds++;
      addWeight(1);
      if (rnd(6) == 0 && !flag(F_SICK)) makeSick();
    } else {
      setMode(M_REFUSE);
      return;
    }
  } else {
    if (pet.weight >= 99) { setMode(M_REFUSE); return; }
    addWeight(2);
    if (pet.strength < 4) pet.strength++;
  }
  eatType = t;
  setMode(M_EAT);
  phase = 0xFF;
}

void modeFeed(uint8_t ev) {
  if (ev & BTN_A) sub ^= 1;
  if (ev & BTN_C) { setMode(M_HOME); return; }
  if (ev & BTN_B) { feed(sub); return; }
  textCenter(4, 1, F("FEED"));
  icon(SPR_I_FEED, 24, 25, 2);
  icon(SPR_PILL0, 88, 25, 2);
  display.drawRect(sub ? 82 : 18, 19, 28, 28, C_ON);
  textAt(20, 52, 1, F("Meat"));
  textAt(75, 52, 1, F("Vitamin"));
}

const uint8_t* foodSprite(uint8_t type, uint8_t bite) {
  if (type == 0) return bite == 0 ? SPR_I_FEED : bite == 1 ? SPR_MEAT1 : SPR_MEAT2;
  return bite == 0 ? SPR_PILL0 : bite == 1 ? SPR_PILL1 : SPR_PILL2;
}

void modeEat(uint8_t ev) {
  unsigned long t = modeElapsed();
  if (t >= 1800) { setMode(M_HOME); return; }
  uint8_t bite = t / 600;
  if (bite != phase) { phase = bite; play(MEL_EAT); }
  icon(foodSprite(eatType, bite), 16, 40, 2);
  drawMon(pet.species, (t / 300) & 1, 56, 24, false);
}

// ---------------------------------------------------------------- training mini-game
// A cursor sweeps across a gauge: stop it inside the target zone with B. 5 rounds,
// 3+ hits = +1 strength heart. Every 4 sessions = +1 effort heart.
void newRound() {
  phase = 0;
  phaseFlag = false;
  zoneW = 26 - gRound * 3;
  zoneX = rndRange(4, 96 - zoneW);
  gCursor = 0;
  gDir = 1;
}

void startTrain() {
  setMode(M_TRAIN);
  gRound = 0;
  gHits = 0;
  gMask = 0;
  myMouth = mouthOf(pet.species);
  newRound();
}

void finishTrain() {
  if (pet.trainings < 255) pet.trainings++;
  if (++pet.effortSub >= 4) {
    pet.effortSub = 0;
    if (pet.effort < 4) pet.effort++;
  }
  addWeight(-2);
  if (gHits >= 3) {
    if (pet.strength < 4) pet.strength++;
    startHappy();
  } else {
    startSad();
  }
}

void modeTrain(uint8_t ev) {
  unsigned long now = millis();
  if (phase == 0) {
    if (ev & BTN_C) { setMode(M_HOME); return; }
    gCursor += gDir * (3 + gRound);
    if (gCursor >= 99) { gCursor = 99; gDir = -1; }
    else if (gCursor <= 0) { gCursor = 0; gDir = 1; }
    if (ev & BTN_B) {
      shotHit = gCursor >= zoneX && gCursor <= zoneX + zoneW;
      if (shotHit) { gHits++; gMask |= 1 << gRound; }
      phase = 1;
      phaseT = now;
      phaseFlag = false;
      play(MEL_SHOOT);
    }
  }
  unsigned long t = now - phaseT;
  if (phase == 1) {
    if (t >= 400 && !phaseFlag) { phaseFlag = true; if (shotHit) play(MEL_HIT); }
    if (t >= 1000) {
      if (++gRound >= 5) { finishTrain(); return; }
      newRound();
    }
  }

  textAt(2, 4, 1, F("TRAIN"));
  for (uint8_t i = 0; i < 5; i++) {
    int16_t x = 76 + i * 10;
    if (gMask & (1 << i)) display.fillRect(x, 4, 7, 7, C_ON);
    else display.drawRect(x, 4, 7, 7, C_ON);
  }
  bool shooting = phase == 1 && t < 400;
  drawMon(pet.species, shooting ? 1 : 0, 4, 17, true);
  int16_t dx = 88;
  if (phase == 1 && shotHit && t >= 400 && t < 800) dx += ((t / 50) & 1) ? 3 : -3;
  drawSprite(SPR_DUMMY, dx, 17, 16, 16, 2, false, C_ON);
  if (phase == 1) {
    // from the pet's mouth to the dummy's edge (x 90) in 400 ms
    int16_t mx = mouthScreenX(myMouth, 4, true), my = mouthScreenY(myMouth, 17);
    int16_t sx = mx + (int16_t)((int32_t)t * (90 - mx) / 400);
    int16_t sy = shotHit ? my : my - (int16_t)(t / 50);   // a miss flies over the dummy
    if (t < 400) {
      drawShot(sx, sy, true);
    } else {
      if (!shotHit && t < 700) drawShot(sx, sy, true);
      textAt(50, 20, 1, shotHit ? F("HIT!") : F("MISS"));
    }
  }
  display.drawRect(13, 52, 102, 10, C_ON);
  display.fillRect(14 + zoneX, 53, zoneW, 8, C_ON);
  display.fillRect(13 + gCursor, 50, 3, 14, C_INV);
}

// ---------------------------------------------------------------- battle
void startBattle() {
  int8_t st = (int8_t)sp.stage + (int8_t)rndRange(-1, 2);
  if (st < ST_ROOKIE) st = ST_ROOKIE;
  if (st > ST_ULTIMATE) st = ST_ULTIMATE;
  uint8_t n = 0;
  for (uint8_t i = 0; i < SP_COUNT; i++)
    if (pgm_read_byte(&SPECIES[i].stage) == (uint8_t)st) n++;
  uint8_t k = rnd(n);
  enemyId = pet.species;            // fallback if no species exists at that stage
  for (uint8_t i = 0; i < SP_COUNT; i++) {
    if (pgm_read_byte(&SPECIES[i].stage) != (uint8_t)st) continue;
    if (k == 0) { enemyId = i; break; }
    k--;
  }
  int16_t myP = sp.power + pet.strength * 3 + pet.effort * 2;
  if (pet.weight > sp.baseWeight * 2) myP -= myP / 5;     // too heavy = sluggish
  int16_t enP = pgm_read_byte(&SPECIES[enemyId].power) + rndRange(0, 13);
  int16_t hc = (int32_t)myP * 100 / (myP + enP);
  linkFight = false;
  beginBattle(constrain(hc, 15, 85));
}

void beginBattle(uint8_t hc) {
  hitChance = hc;
  myHp = enHp = 3;
  myMouth = mouthOf(pet.species);
  enMouth = mouthOf(enemyId);
  setMode(M_BATTLE);
}

void nextShot(uint8_t who) {
  phase = who;
  phaseT = millis();
  phaseFlag = false;
  uint8_t r = rnd(100);
  shotHit = (who == 1) ? (r < hitChance) : (r >= hitChance);
  // a linked battle must end the way the two devices agreed: the loser never lands the last hit
  if (linkFight && (who == 1 ? !battleWon && enHp == 1 : battleWon && myHp == 1)) shotHit = false;
  play(MEL_SHOOT);
}

void finishBattle() {
  pet.stageBattles++;
  pet.totalBattles++;
  if (battleWon) { pet.stageWins++; pet.totalWins++; }
  addWeight(-4);
  if (rnd(100) < (battleWon ? INJURY_CHANCE_WIN : INJURY_CHANCE_LOSE)) {
    injure();
    showMsg(F("Injured!"));
  } else {
    setMode(M_HOME);
  }
}

void modeBattle(uint8_t ev) {
  unsigned long now = millis();
  unsigned long t = now - phaseT;
  switch (phase) {
    case 0:
      if (t >= 1500) nextShot(1);
      break;
    case 1: case 2:
      if (t >= 500 && !phaseFlag) { phaseFlag = true; if (shotHit) play(MEL_HIT); }
      if (t >= 900) {
        if (shotHit) { if (phase == 1) enHp--; else myHp--; }
        if (enHp == 0 || myHp == 0) {
          battleWon = enHp == 0;
          phase = 3;
          phaseT = now;
          if (battleWon) play(MEL_WIN); else play(MEL_LOSE);
        } else {
          nextShot(phase == 1 ? 2 : 1);
        }
      }
      break;
    case 3:
      if (t >= 2000) { finishBattle(); return; }
      break;
  }
  t = millis() - phaseT;

  for (uint8_t i = 0; i < 3; i++) {
    if (i < myHp) display.fillRect(2 + i * 8, 5, 6, 5, C_ON);
    else display.drawRect(2 + i * 8, 5, 6, 5, C_ON);
    if (i < enHp) display.fillRect(104 + i * 8, 5, 6, 5, C_ON);
    else display.drawRect(104 + i * 8, 5, 6, 5, C_ON);
  }

  int16_t myY = 24, enY = 24;
  bool myVis = true, enVis = true;
  uint8_t myFr = 0, enFr = 0;

  if (phase == 0) {
    textCenter(32, 2, F("VS"));
    char name[12];
    strcpy_P(name, SPECIES[enemyId].name);
    textCenterRam(4, 1, name);
  } else if (phase == 1 || phase == 2) {
    bool mine = phase == 1;
    // shots leave the shooter's mouth and reach the other side's edge (x 94 / 33) at 500 ms
    int16_t sx = mine ? mouthScreenX(myMouth, 0, true) : mouthScreenX(enMouth, 96, false);
    int16_t sy = mine ? mouthScreenY(myMouth, 24) : mouthScreenY(enMouth, 24);
    int16_t ex = mine ? 94 : 33;
    if (t < 500) {
      if (mine) myFr = 1; else enFr = 1;
      drawShot(sx + (int16_t)((int32_t)t * (ex - sx) / 500), sy, mine);
    } else {
      if (shotHit) {
        bool vis = (t / 80) & 1;
        if (mine) enVis = vis; else myVis = vis;
      } else {
        if (mine) enY = 16; else myY = 16;            // dodge!
        int16_t d = (t - 500) / 8;
        drawShot(mine ? ex + d : ex - d, sy, mine);
      }
    }
  } else {
    textCenter(4, 1, battleWon ? F("WIN!") : F("LOSE"));
    bool up = (t / 250) & 1;
    if (battleWon) { myFr = 1; myY = up ? 18 : 24; }
    else           { enFr = 1; enY = up ? 18 : 24; }
  }
  if (myVis) drawMon(pet.species, myFr, 0, myY, true);
  if (enVis) drawMon(enemyId, enFr, 96, enY, false);
}

// ---------------------------------------------------------------- A-Com link battle
#if ACOM_ENABLED
// Battles a real Digital Monster (original, or a newer model's "other battle") through the
// A-Com circuit, following the DMComm project by BladeSabre (MIT):
// https://dmcomm.github.io/orig_battle/  https://wiki.humulos.com/wiki/DMOG_DigiROM_format
//
// The two devices take turns on one wire that idles high (~2.9 V). Each packet starts with the
// line pulled low for ~59 ms, then a start bit and 16 data bits, LSB first, as pulse widths.
// The low byte holds two nibbles and the high byte is its bitwise NOT (a check):
//   packet 1: (boost << 4) | slot          slot 3..14 = A..L on the growth chart
//   packet 2: (version << 4) | outcome     outcome 1 = win, 2 = loss, for the sender
// Order: first P1, second P1, first P2, second P2. The side that goes first decides who won.

#define ACOM_LOW_LEVEL 389          // analogRead below this (1.9 V) = line pulled low

// Odds out of 16 that the row slot beats the column slot (Ver.1 table, Humulos wiki),
// two columns per byte: A..L = 3..14.
#define OD(a, b) (uint8_t)((a) << 4 | (b))
const uint8_t ACOM_ODDS[12][6] PROGMEM = {
  {OD(8, 8),   OD(2, 3),   OD(2, 3),   OD(2, 3),   OD(7, 1),   OD(1, 1)},     // A
  {OD(8, 8),   OD(2, 3),   OD(2, 3),   OD(2, 3),   OD(7, 1),   OD(1, 1)},     // B
  {OD(15, 15), OD(8, 11),  OD(9, 11),  OD(7, 11),  OD(13, 3),  OD(3, 3)},     // C
  {OD(13, 13), OD(5, 8),   OD(5, 9),   OD(5, 7),   OD(11, 2),  OD(2, 2)},     // D
  {OD(15, 15), OD(7, 11),  OD(8, 11),  OD(9, 11),  OD(13, 3),  OD(3, 3)},     // E
  {OD(13, 13), OD(5, 7),   OD(5, 8),   OD(5, 9),   OD(11, 2),  OD(2, 2)},     // F
  {OD(15, 15), OD(9, 11),  OD(7, 11),  OD(8, 11),  OD(13, 3),  OD(3, 3)},     // G
  {OD(13, 13), OD(5, 9),   OD(5, 7),   OD(5, 8),   OD(11, 2),  OD(2, 2)},     // H
  {OD(9, 9),   OD(3, 5),   OD(3, 5),   OD(3, 5),   OD(8, 1),   OD(1, 1)},     // I
  {OD(15, 15), OD(13, 14), OD(13, 14), OD(13, 14), OD(15, 8),  OD(5, 5)},     // J
  {OD(15, 15), OD(13, 14), OD(13, 14), OD(13, 14), OD(15, 11), OD(8, 5)},     // K
  {OD(15, 15), OD(13, 14), OD(13, 14), OD(13, 14), OD(15, 11), OD(11, 8)},    // L
};
#undef OD

uint16_t acomFoe;                   // the opponent's packet 1 (boost, slot)
uint32_t acomEdgeT;                 // micros() at the last level change seen

bool acomLow() { return analogRead(PIN_ACOM_IN) < ACOM_LOW_LEVEL; }

// Waits for the line to go low (true) or idle (false). Returns how long the level that just
// ended lasted, in µs, if that was between minUs and maxUs; otherwise 0.
uint32_t acomPhase(bool low, uint32_t minUs, uint32_t maxUs) {
  for (;;) {
    uint32_t now = micros(), d = now - acomEdgeT;
    if (acomLow() == low) { acomEdgeT = now; return d >= minUs ? d : 0; }
    if (d > maxUs) return 0;
  }
}

// Receives one packet. waitUs: how long the sender may take to start.
bool acomRecv(uint16_t& out, uint32_t waitUs) {
  acomEdgeT = micros();
  if (!acomPhase(true, 0, waitUs)) return false;            // the sender pulls the line low...
  if (!acomPhase(false, 30000, 90000)) return false;        // ...for ~59 ms
  if (!acomPhase(true, 1400, 2500)) return false;           // start bit: ~2.1 ms high,
  if (!acomPhase(false, 300, 1400)) return false;           //            ~0.9 ms low
  uint16_t bits = 0;
  for (uint8_t i = 0; i < 16; i++) {
    uint32_t d = acomPhase(true, 400, 3400);                // high ~0.85 ms = 0, ~2.7 ms = 1
    if (!d) return false;
    bits >>= 1;
    if (d > 1800) bits |= 0x8000;
    if (!acomPhase(false, 800, 4000)) return false;         // then low for the rest of the bit
  }
  out = bits;
  return true;
}

void acomDrive(bool low, uint16_t us) {
  FAST_WRITE(PIN_ACOM_OUT, !low);
  delayMicroseconds(us);
}

void acomSend(uint16_t bits) {
  acomDrive(false, 3000);
  FAST_LOW(PIN_ACOM_OUT);
  delay(59);
  acomDrive(false, 2083);
  acomDrive(true, 917);
  for (uint8_t i = 0; i < 16; i++, bits >>= 1) {
    bool one = bits & 1;
    acomDrive(false, one ? 2667 : 850);
    acomDrive(true, one ? 1667 : 3167);
  }
  acomDrive(false, 400);            // back to idle
}

uint16_t acomPack(uint8_t hi, uint8_t lo) {
  uint8_t b = (hi << 4) | (lo & 15);
  return (uint16_t)(uint8_t)~b << 8 | b;
}
bool acomValid(uint16_t p) { return (uint8_t)(p >> 8) == (uint8_t)~p; }

// The growth-chart slot a species fights as: its acomSlot, or a default for its stage.
uint8_t acomSlotOf(uint8_t id) {
  uint8_t s = pgm_read_byte(&SPECIES[id].acomSlot);
  if (s >= 3 && s <= 14) return s;
  uint8_t st = pgm_read_byte(&SPECIES[id].stage);
  return st >= ST_ULTIMATE ? 12 : st == ST_CHAMPION ? 7 : 3;
}

uint8_t acomBoost() { return pet.effort < ACOM_MAX_BOOST ? pet.effort : ACOM_MAX_BOOST; }

// Going first, BitBeast rolls the result the way an original V-pet does: by the slot table.
bool acomDecide(uint8_t mySlot) {
  uint8_t ts = acomFoe & 15, tb = (acomFoe >> 4) & 15;
  if (ts < 3) ts = 3;
  if (ts > 14) ts = 14;
  ts -= 3;
  uint8_t b = pgm_read_byte(&ACOM_ODDS[mySlot - 3][ts >> 1]);
  int8_t odds = (ts & 1) ? (b & 15) : (b >> 4);
  odds += (int8_t)acomBoost() - (int8_t)tb;      // 1/16 per boost level: a guess, not measured
  if (pet.weight > sp.baseWeight * 2) odds -= 2; // too heavy = sluggish, as in CPU battles
  return (int8_t)rnd(16) < constrain(odds, 1, 15);
}

// True if C is pressed or the time is up: ends the waiting loops below.
bool acomGiveUp(unsigned long start) {
  return (readRaw() & BTN_C) || millis() - start > ACOM_LISTEN_S * 1000UL;
}

// One complete exchange. The screen is frozen meanwhile: the pulse timing must not be
// interrupted by a display update. first = BitBeast starts and decides the result.
// Returns 0 = done (battleWon holds the result), 1 = no answer or cancelled, 2 = garbled.
uint8_t acomRun(bool first) {
  uint8_t mySlot = acomSlotOf(pet.species);
  uint16_t mine = acomPack(acomBoost(), mySlot), res;
  unsigned long start = millis();
  melPtr = NULL;
  noTone(PIN_BUZZER);
  if (first) {
    for (;;) {                                     // knock once a second until the toy answers
      acomSend(mine);
      if (acomRecv(acomFoe, 100000UL)) break;
      unsigned long t = millis();
      while (millis() - t < 1000) if (acomGiveUp(start)) return 1;
    }
    if (!acomValid(acomFoe)) return 2;
    battleWon = acomDecide(mySlot);
    acomSend(acomPack(ACOM_VERSION, battleWon ? 1 : 2));
    if (!acomRecv(res, 100000UL) || !acomValid(res)) return 2;   // the toy's reply
  } else {
    while (!acomRecv(acomFoe, 200000UL)) if (acomGiveUp(start)) return 1;
    if (!acomValid(acomFoe)) return 2;
    acomSend(mine);
    if (!acomRecv(res, 100000UL) || !acomValid(res)) return 2;
    uint8_t o = res & 15;                          // the toy's result, from its side
    if (o != 1 && o != 2) return 2;
    battleWon = o == 2;
    acomSend(acomPack(ACOM_VERSION, battleWon ? 1 : 2));
  }
  return 0;
}

// The BitBeast species that stands in for the opponent: same slot, else same stage.
uint8_t acomFoeSpecies() {
  uint8_t slot = acomFoe & 15;
  uint8_t st = slot >= 12 ? ST_ULTIMATE : slot >= 5 ? ST_CHAMPION : ST_ROOKIE;
  for (uint8_t i = 0; i < SP_COUNT; i++)
    if (slot >= 3 && pgm_read_byte(&SPECIES[i].acomSlot) == slot) return i;
  for (uint8_t i = 0; i < SP_COUNT; i++)
    if (pgm_read_byte(&SPECIES[i].stage) == st) return i;
  return pet.species;
}

// Battle menu: a CPU battle, or a link battle going first or second.
void modeLink(uint8_t ev) {
  if (ev & BTN_A) sub = sub < 2 ? sub + 1 : 0;
  if (ev & BTN_C) { setMode(M_HOME); return; }
  if (ev & BTN_B) {
    if (sub == 0) { startBattle(); return; }
    display.clearDisplay();
    textCenter(4, 1, F("LINK"));
    textCenter(28, 1, sub == 1 ? F("Calling V-pet...") : F("Waiting for V-pet"));
    textCenter(48, 1, F("C: cancel"));
    display.display();
    uint8_t r = acomRun(sub == 1);
    btnStable = btnLastRaw = readRaw();            // a held C must not count as a new press
    wakeScreen();
    if (r == 0) {
      enemyId = acomFoeSpecies();
      linkFight = true;
      beginBattle(battleWon ? 65 : 35);
    } else {
      showMsg(r == 1 ? F("No link") : F("Link error"));
    }
    return;
  }
  textCenter(4, 1, F("BATTLE"));
  option(10, 20, F("vs CPU"), sub == 0);
  option(10, 33, F("Link: go 1st"), sub == 1);
  option(10, 46, F("Link: go 2nd"), sub == 2);
}
#endif

// ---------------------------------------------------------------- flush / lights / heal
void modeFlush(uint8_t ev) {
  unsigned long t = modeElapsed();
  if (t >= 1200) {
    bool had = pet.poops > 0;
    pet.poops = 0;
    if (had) startHappy(); else setMode(M_HOME);
    return;
  }
  drawHome();
  int16_t wx = 128 - (int16_t)(t * 128 / 1000);
  if (wx < 0) wx = 0;
  display.fillRect(wx, 16, 128 - wx, 48, C_OFF);
  display.setTextSize(1);
  for (int16_t y = 18; y < 62; y += 8) {
    display.setCursor(wx + ((y >> 3) & 1) * 3, y);
    display.print('~');
  }
}

void modeLights(uint8_t ev) {
  if (ev & BTN_A) sub ^= 1;
  if (ev & BTN_C) { setMode(M_HOME); return; }
  if (ev & BTN_B) { setFlag(F_LIGHTSOFF, sub == 1); setMode(M_HOME); return; }
  textCenter(4, 1, F("LIGHTS"));
  icon(SPR_I_LIGHT, 52, 19, 3);
  option(30, 52, F("ON"), sub == 0);
  option(82, 52, F("OFF"), sub == 1);
}

void modeHeal(uint8_t ev) {
  unsigned long t = modeElapsed();
  if (t >= 1200) {
    if (pet.medsNeeded) pet.medsNeeded--;
    if (pet.medsNeeded == 0) {
      setFlag(F_SICK | F_INJURED, false);
      pet.sickMin = 0;
      startHappy();
    } else {
      showMsg(F("Needs more..."));
    }
    return;
  }
  int16_t x = t < 800 ? t * 40 / 800 : 40;
  icon(SPR_SYRINGE, x, 38, 2);
  drawMon(pet.species, 0, 60, 24, false);
}

// ---------------------------------------------------------------- setup menu / clock
void modeSetup(uint8_t ev) {
  if (ev & BTN_A) sub = (sub + 1) & 3;
  if (ev & BTN_C) { setMode(M_HOME); return; }
  if (ev & BTN_B) {
    switch (sub) {
      case 0:
        clockH = pet.clockMin / 60;
        clockM = pet.clockMin % 60;
        setMode(M_CLOCKSET);
        return;
      case 1:
        pet.flags ^= F_SOUNDOFF;
        if (flag(F_SOUNDOFF)) { melPtr = NULL; noTone(PIN_BUZZER); }
        saveNow();
        break;
      case 2:
        pet.speedIdx = (pet.speedIdx + 1) & 3;
        lastMinuteMs = millis();
        saveNow();
        break;
      case 3:
        setMode(M_CONFIRM);
        return;
    }
  }
  textCenter(4, 1, F("SETUP"));
  for (uint8_t i = 0; i < 4; i++) {
    int16_t y = 19 + i * 11;
    bool s = sub == i;
    if (s) display.fillRect(0, y - 2, 128, 11, C_ON);
    display.setTextColor(s ? C_OFF : C_ON);
    display.setTextSize(1);
    display.setCursor(4, y);
    switch (i) {
      case 0: display.print(F("Set clock")); break;
      case 1:
        display.print(F("Sound"));
        display.setCursor(88, y);
        display.print(flag(F_SOUNDOFF) ? F("OFF") : F("ON"));
        break;
      case 2:
        display.print(F("Speed"));
        display.setCursor(88, y);
        display.print('x');
        display.print(SPEEDS[pet.speedIdx]);
        break;
      case 3: display.print(F("New egg")); break;
    }
  }
  display.setTextColor(C_ON);
}

void modeConfirm(uint8_t ev) {
  if (ev & BTN_B) { newEgg(); return; }
  if (ev & (BTN_A | BTN_C)) { setMode(M_SETUP); sub = 3; return; }
  textCenter(4, 1, F("Start a new egg?"));
  textCenter(28, 1, F("Current pet is lost"));
  textCenter(48, 1, F("B:yes  C:no"));
}

void modeClockSet(uint8_t ev, uint8_t rep) {
  if ((ev | rep) & BTN_A) {
    if (phase == 0) clockH = (clockH + 1) % 24;
    else clockM = (clockM + 1) % 60;
  }
  if (ev & BTN_B) {
    if (phase == 0) phase = 1;
    else {
      pet.clockMin = clockH * 60 + clockM;
      lastMinuteMs = millis();
      saveNow();
      setMode(M_HOME);
      return;
    }
  }
  if (ev & BTN_C) { setMode(M_SETUP); return; }
  textCenter(4, 1, F("SET CLOCK"));
  drawTime(clockH, clockM, 22, true);
  display.fillRect(phase ? 73 : 19, 47, 34, 2, C_ON);
  textCenter(55, 1, F("A:+  B:next"));
}

void modeClock(uint8_t ev) {
  if (ev || modeElapsed() > 6000) { setMode(M_HOME); return; }
  uint8_t h = pet.clockMin / 60;
  icon((h >= 6 && h < 18) ? SPR_SUN : SPR_MOON, 60, 4, 1);
  drawTime(h, pet.clockMin % 60, 26, blink());
  if (flag(F_ASLEEP)) textCenter(54, 1, F("sleeping"));
}

// ---------------------------------------------------------------- evolution / death
void startEvolve(uint8_t to) {
  evoFrom = pet.species;
  evoTo = to;
  evolvePending = SP_NONE;
  sel = -1;
  setMode(M_EVOLVE);
}

void modeEvolve(uint8_t ev) {
  unsigned long t = modeElapsed();
  if (phase == 0) {
    if (t < 3000) {
      bool showNew = ((t * t) / 150000UL) & 1;     // flicker speeds up
      int16_t wob = (pgm_read_byte(&SPECIES[evoFrom].stage) == ST_EGG && blinkFast()) ? 2 : 0;
      drawMon(showNew ? evoTo : evoFrom, 0, 48 + wob, 24, false);
      return;
    }
    applyEvolution(evoTo);
    play(MEL_EVOLVE);
    phase = 1;
  }
  if (t >= 5500) { setMode(M_HOME); return; }
  drawMon(pet.species, (t / 300) & 1, 48, 24, false);
  textCenterRam(4, 1, sp.name);
}

void modeDead(uint8_t ev) {
  if ((ev & BTN_B) && modeElapsed() > 2000) { newEgg(); return; }
  textCenter(4, 1, F("R.I.P."));
  drawSprite(SPR_TOMB, 48, 16, 16, 16, 2, false, C_ON);
  textCenterRam(48, 1, sp.name);
  if (blink()) {
    textCenter(56, 1, F("B: new egg"));
  } else {
    display.setCursor(28, 56);
    display.print(F("Age "));
    display.print(pet.age);
    display.print(F("d  Gen "));
    display.print(pet.generation);
  }
}

// ---------------------------------------------------------------- main loop
void runMode(uint8_t ev, uint8_t rep) {
  switch (mode) {
    case M_HOME:     modeHome(ev); break;
    case M_MSG:      modeMsg(ev); break;
    case M_STATS:    modeStats(ev); break;
    case M_FEED:     modeFeed(ev); break;
    case M_EAT:      modeEat(ev); break;
    case M_REFUSE:   modeRefuse(ev); break;
    case M_HAPPY:    modeHappy(ev); break;
    case M_SAD:      modeSad(ev); break;
    case M_TRAIN:    modeTrain(ev); break;
    case M_BATTLE:   modeBattle(ev); break;
    case M_FLUSH:    modeFlush(ev); break;
    case M_LIGHTS:   modeLights(ev); break;
    case M_HEAL:     modeHeal(ev); break;
    case M_SETUP:    modeSetup(ev); break;
    case M_CONFIRM:  modeConfirm(ev); break;
    case M_CLOCKSET: modeClockSet(ev, rep); break;
    case M_CLOCK:    modeClock(ev); break;
    case M_EVOLVE:   modeEvolve(ev); break;
    case M_DEAD:     modeDead(ev); break;
#if ACOM_ENABLED
    case M_LINK:     modeLink(ev); break;
#endif
  }
}

void frame() {
  uint8_t ev = evPress, rep = evRepeat;
  evPress = evRepeat = 0;

  checkCallAlarm();
  if (evolvePending != SP_NONE) wakeScreen();

  unsigned long idle = millis() - lastInput;
  if (idle > 30000UL && (mode == M_STATS || mode == M_FEED || mode == M_LIGHTS ||
                         mode == M_SETUP || mode == M_CONFIRM || mode == M_MSG || mode == M_LINK)) {
    setMode(M_HOME);
  }
  if (DIM_AFTER_MS && !dimmed && idle > DIM_AFTER_MS) { display.dim(true); dimmed = true; }
  if (SCREEN_OFF_MS && !screenOff && idle > SCREEN_OFF_MS &&
      (mode == M_HOME || mode == M_DEAD) && !anyCall()) {
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    screenOff = true;
  }
  if (screenOff) return;

  display.clearDisplay();
  uint8_t before = mode;
  runMode(ev, rep);
  if (mode != before) {            // mode changed: draw the new mode right away (no blank frame)
    display.clearDisplay();
    runMode(0, 0);
  }
  display.display();
}

// ---------------------------------------------------------------- kiosk / screen test
#if KIOSK_MODE
uint8_t kioskId = SP_NONE;
unsigned long kioskT;
bool kioskAttack;                   // this showing: stand at an edge and fire, instead of walking
uint8_t kioskMouth;

void kioskNext() {
  uint8_t n;
  do { n = rnd(SP_COUNT); } while (SP_COUNT > 1 && n == kioskId);
  kioskId = n;
  kioskT = millis();
  pet.species = n;
  loadSpecies();
  petLeft = rnd(2);
  kioskAttack = sp.stage != ST_EGG && rnd(2);   // about half the time
  if (kioskAttack) petX = petLeft ? petMaxX : 0;    // stand at one edge, facing the other
  else petX = rndRange(0, petMaxX + 1);
  kioskMouth = mouthOf(n);
}

// All pixels on, checkerboard, then a border: makes dead pixels and wiring faults obvious.
void screenTest() {
  display.clearDisplay();
  display.fillRect(0, 0, 128, 64, C_ON);
  display.display();
  delay(800);
  display.clearDisplay();
  for (int16_t y = 0; y < 64; y++)
    for (int16_t x = (y & 1); x < 128; x += 2) display.drawPixel(x, y, C_ON);
  display.display();
  delay(800);
  display.clearDisplay();
  display.drawRect(0, 0, 128, 64, C_ON);
  textCenter(4, 1, F("SCREEN TEST OK"));
  textCenter(36, 1, F("kiosk mode"));
  display.display();
  delay(1200);
}

void kioskFrame(uint8_t ev) {
  if ((ev & BTN_B) || millis() - kioskT >= KIOSK_SHOW_MS) kioskNext();
  if (!kioskAttack) updateWander();
  // Header in rows 0-15 (the yellow band on two-colour modules): name left, stage right.
  char buf[10];
  strcpy_P(buf, STAGE_NAMES[sp.stage]);
  display.setTextSize(1);
  display.setCursor(2, 4);
  display.print(sp.name);
  display.setCursor(126 - strlen(buf) * 6, 4);
  display.print(buf);
  // Creature centred in the 48 blue rows below (y 24..55), alternating idle / action frames.
  if (sp.stage == ST_EGG) {
    drawMon(kioskId, petBob, 48 + (blinkFast() ? 2 : -2), 24, false);
  } else if (kioskAttack) {
    // 1.4 s cycle: wind up (idle frame), then fire (action frame) while the shot crosses the screen
    unsigned long t = (millis() - kioskT) % 1400;
    bool firing = t >= 400 && t < 1000;
    int16_t recoil = (t >= 400 && t < 500) ? (petLeft ? 2 : -2) : 0;
    drawMon(kioskId, firing ? 1 : 0, petX + recoil, 24, !petLeft);
    if (firing) {
      int16_t mx = mouthScreenX(kioskMouth, petX + recoil, !petLeft);
      int16_t my = mouthScreenY(kioskMouth, 24);
      int16_t d = (int16_t)((t - 400) * 120 / 600);    // 0..120 px (32-bit math: 16-bit overflows)
      drawShot(petLeft ? mx - d : mx + d, my, !petLeft);
    }
  } else {
    drawMon(kioskId, petBob, petX, 24, !petLeft);
  }
}
#endif

void setup() {
#if BUTTONS_ACTIVE_LOW
  FAST_INPUT(PIN_BTN_A); FAST_HIGH(PIN_BTN_A);   // input with pull-up
  FAST_INPUT(PIN_BTN_B); FAST_HIGH(PIN_BTN_B);
  FAST_INPUT(PIN_BTN_C); FAST_HIGH(PIN_BTN_C);
#else
  FAST_INPUT(PIN_BTN_A); FAST_LOW(PIN_BTN_A);    // plain input (external pull-downs)
  FAST_INPUT(PIN_BTN_B); FAST_LOW(PIN_BTN_B);
  FAST_INPUT(PIN_BTN_C); FAST_LOW(PIN_BTN_C);
#endif
  FAST_OUTPUT(PIN_BUZZER);
  FAST_OUTPUT(PIN_LED);
#if ACOM_ENABLED
  FAST_HIGH(PIN_ACOM_OUT);          // idle: the divider holds the toy's line at ~2.9 V
  FAST_OUTPUT(PIN_ACOM_OUT);
#endif

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    for (;;) { FAST_TOGGLE(PIN_LED); delay(150); }   // no display / no RAM
  }
  display.setTextColor(C_ON);
  display.setTextWrap(false);

#if KIOSK_MODE
  memset(&pet, 0, sizeof(pet));     // nothing is loaded from or saved to EEPROM
  rndSeed(analogRead(A0) ^ micros());
  screenTest();
  petMaxX = 96;                     // no poop in kiosk mode: use the full width
  kioskNext();
  lastFrame = millis();
  return;
#endif

  bool loaded = loadGame();
  rndSeed(analogRead(A0) ^ ((uint32_t)saveSeq << 8) ^ micros());

  // splash
  display.clearDisplay();
  textCenter(1, 2, F("BitBeast"));
  drawMon(0, 0, 48, 21, false);
  textCenter(56, 1, F("v-pet for UNO"));
  display.display();
  play(MEL_START);
  waitMs(2200);

  lastInput = lastSave = lastFrame = lastMinuteMs = millis();
  if (loaded) {
    loadSpecies();
    setMode(flag(F_DEAD) ? M_DEAD : M_HOME);
  } else {
    memset(&pet, 0, sizeof(pet));
    pet.clockMin = 12 * 60;
    newEgg();                       // generation 1
    clockH = 12;
    clockM = 0;
    setMode(M_CLOCKSET);            // first boot: set the clock
  }
}

void loop() {
  pollButtons();
  updateSound();
  unsigned long now = millis();
#if KIOSK_MODE
  if (now - lastFrame >= FRAME_MS) {
    lastFrame = now;
    uint8_t ev = evPress;
    evPress = evRepeat = 0;
    display.clearDisplay();
    kioskFrame(ev);
    display.display();
  }
#else
  unsigned long mm = 60000UL / SPEEDS[pet.speedIdx];
  while (now - lastMinuteMs >= mm) {
    lastMinuteMs += mm;
    gameMinute();
  }
  if (now - lastFrame >= FRAME_MS) {
    lastFrame = now;
    frame();
  }
  if (now - lastSave >= SAVE_INTERVAL_MS) saveNow();
#endif
}
