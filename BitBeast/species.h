// Species table and evolution rules for BitBeast.
// Edit visually with tools/evolution-tool.html (or by hand, carefully).
#pragma once
#include "types.h"
#include "sprites.h"

enum SpeciesId : uint8_t {
  SP_EGG,
  SP_BOTAMON,
  SP_KOROMON,
  SP_AGUMON,
  SP_GREYMON,
  SP_MGREYMONV,
  SP_MAMEMON,
  SP_MONZAEMON,
  SP_KABUTERIMON,
  SP_ANGEMON,
  SP_NUMEMON,
  SP_VADEMON,
  SP_DIGITAMAMON,
  SP_COUNT,
  SP_NONE = 0xFF
};

const Species SPECIES[SP_COUNT] PROGMEM = {
  // name, stage, hungerMin, strengthMin, poopMin, sleepHour, wakeHour, power, weight, evolveMin, lifespanDays, acomSlot, {frame0, frame1}
  {"Egg",         ST_EGG,      0,  0,  0,   NOSLEEP, 0,  0,  5,  2,    0,  0,  {SPR_EGG0,      SPR_EGG1}},
  {"Botamon",     ST_FRESH,    3,  3,  3,   NOSLEEP, 0,  2,  5,  10,   0,  0,  {SPR_BLIPP0,    SPR_BLIPP1}},
  {"Koromon",     ST_TRAINING, 30, 30, 60,  20,      9,  4,  10, 360,  0,  0,  {SPR_SQUEEK0,   SPR_SQUEEK1}},
  {"Agumon",      ST_ROOKIE,   48, 48, 120, 20,      8,  10, 20, 1440, 0,  3,  {SPR_EMBRIX0,   SPR_EMBRIX1}},
  {"Greymon",     ST_CHAMPION, 59, 38, 120, 21,      9,  8,  20, 1440, 0,  5,  {SPR_MOSSUP0,   SPR_MOSSUP1}},
  {"MGreymonV",   ST_ULTIMATE, 59, 59, 120, 23,      9,  26, 30, 2160, 15, 12, {SPR_PYROSAUR0, SPR_PYROSAUR1}},
  {"Mamemon",     ST_CHAMPION, 48, 59, 120, 21,      7,  18, 20, 2160, 15, 13, {SPR_GLIMWING0, SPR_GLIMWING1}},
  {"Monzaemon",   ST_ULTIMATE, 70, 48, 120, 22,      8,  22, 40, 2160, 15, 14, {SPR_IRONHORN0, SPR_IRONHORN1}},
  {"Kabuterimon", ST_CHAMPION, 59, 59, 120, 22,      9,  24, 30, 2160, 15, 0,  {SPR_THORNOX0,  SPR_THORNOX1}},
  {"Angemon",     ST_CHAMPION, 48, 48, 120, 20,      8,  16, 25, 2160, 15, 0,  {SPR_TIDEFIN0,  SPR_TIDEFIN1}},
  {"Numemon",     ST_CHAMPION, 38, 38, 90,  20,      10, 10, 15, 2160, 12, 11, {SPR_GRUBBLE0,  SPR_GRUBBLE1}},
  {"Vademon",     ST_ULTIMATE, 70, 70, 180, 23,      9,  40, 45, 0,    20, 0,  {SPR_SOLAREX0,  SPR_SOLAREX1}},
  {"Digitamamon", ST_ULTIMATE, 70, 70, 180, 23,      10, 38, 40, 0,    20, 0,  {SPR_ABYSSOR0,  SPR_ABYSSOR1}},
};

const EvoRule EVO_RULES[] PROGMEM = {
  // from, to, minMistakes, maxMistakes, minTrain, maxTrain, minBattles, minWinPct, minWeight, maxWeight
  {SP_EGG,     SP_BOTAMON, 0, ANY, 0, ANY, 0, 0, 0, ANY},
  {SP_BOTAMON, SP_KOROMON, 0, ANY, 0, ANY, 0, 0, 0, ANY},
  {SP_KOROMON, SP_AGUMON,  0, 2,   0, ANY, 0, 0, 0, ANY},
  {SP_KOROMON, SP_GREYMON, 0, ANY, 0, ANY, 0, 0, 0, ANY},
};
const uint8_t EVO_RULE_COUNT = sizeof(EVO_RULES) / sizeof(EVO_RULES[0]);

const char STAGE_NAMES[6][9] PROGMEM = {"Egg", "Fresh", "In-Train", "Rookie", "Champion", "Ultimate"};
