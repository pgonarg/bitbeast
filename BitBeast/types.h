#pragma once
#include <Arduino.h>

enum Stage : uint8_t { ST_EGG, ST_FRESH, ST_TRAINING, ST_ROOKIE, ST_CHAMPION, ST_ULTIMATE };

// Species IDs (SP_xxx, SP_COUNT, SP_NONE) are generated into species.h.
// Species 0 is always the egg a new generation starts from.

#define NOSLEEP 0xFF
#define ANY     255

// One evolution rule (PROGMEM). Rules are tried in table order; the first rule whose
// `from` is the current species and whose conditions all hold picks the next form.
// Counters are per stage (they reset at every evolution). ANY = no upper limit.
struct EvoRule {
  uint8_t from, to;
  uint8_t minMistakes, maxMistakes;
  uint8_t minTrain, maxTrain;
  uint8_t minBattles;
  uint8_t minWinPct;      // 0 = no requirement; otherwise needs >= 1 battle
  uint8_t minWeight, maxWeight;
};

// One row of the species table (lives in PROGMEM, copied to RAM with memcpy_P).
struct Species {
  char     name[12];      // up to 11 letters
  uint8_t  stage;
  uint8_t  hungerMin;     // game-minutes per hunger heart lost
  uint8_t  strengthMin;   // game-minutes per strength heart lost
  uint8_t  poopMin;       // game-minutes between poops
  uint8_t  sleepHour;     // NOSLEEP = never sleeps
  uint8_t  wakeHour;
  uint8_t  power;         // base battle power
  uint8_t  baseWeight;    // grams
  uint16_t evolveMin;     // game-minutes in this form before it can evolve (0 = final form)
  uint8_t  lifespanDays;  // dies of old age at this age (0 = no limit)
  uint8_t  acomSlot;      // A-Com: original V-pet slot 3..14 (A..L) it fights as; 0 = by stage
  const uint8_t* spr[2];  // frame 0 idle, frame 1 action
};

// Everything that is saved to EEPROM.
struct PetState {
  uint8_t  species;
  uint8_t  hunger, strength;        // 0..4 hearts
  uint8_t  effort, effortSub;       // 0..4 hearts; 4 trainings = 1 heart
  uint8_t  weight;                  // grams
  uint8_t  careMistakes;            // this stage
  uint8_t  trainings;               // this stage
  uint8_t  overfeeds;               // this stage
  uint8_t  injuries;                // lifetime (sickness + injury)
  uint8_t  poops;                   // 0..4 on screen
  uint8_t  medsNeeded;
  uint8_t  hungerTmr, strengthTmr, poopTmr;
  uint8_t  hungerCall, strengthCall, lightsCall;   // minutes each call has been ignored
  uint8_t  age;                     // days since hatching
  uint8_t  generation;
  uint8_t  speedIdx;                // index into SPEEDS[]
  uint8_t  flags;
  uint16_t dayMin;                  // minutes into the current day of life
  uint16_t stageMin;                // minutes in the current form
  uint16_t starveMin, sickMin;
  uint16_t clockMin;                // wall clock, 0..1439
  uint16_t stageBattles, stageWins;
  uint16_t totalBattles, totalWins;
};

#define F_SICK      0x01
#define F_INJURED   0x02
#define F_ASLEEP    0x04
#define F_LIGHTSOFF 0x08
#define F_DEAD      0x10
#define F_SOUNDOFF  0x20
#define F_OVERFED   0x40   // ate one past full; refuses more meat until hunger drops

struct SaveBlock {
  uint16_t magic;
  uint16_t seq;
  PetState s;
  uint8_t  crc;
};

enum Mode : uint8_t {
  M_HOME, M_MSG, M_STATS, M_FEED, M_EAT, M_REFUSE, M_HAPPY, M_SAD,
  M_TRAIN, M_BATTLE, M_FLUSH, M_LIGHTS, M_HEAL, M_SETUP, M_CONFIRM,
  M_CLOCKSET, M_CLOCK, M_EVOLVE, M_DEAD, M_LINK
};
