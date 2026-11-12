#pragma once
// ============================================================
//  BitBeast configuration -- edit this file to match your build
// ============================================================

// ---- Kiosk / screen-test mode ----
// 1 = no game: show a quick screen test, then cycle through random animated creatures
//     forever (nothing is saved, no buttons or buzzer needed). Button B skips to the next one.
// 0 = normal game.
#ifndef KIOSK_MODE                 // (can also be set from the compiler: -DKIOSK_MODE=0)
#define KIOSK_MODE        1
#endif
#define KIOSK_SHOW_MS     6000UL   // how long each creature is shown

// ---- Pins (same layout as Tamaguino) ----
#define PIN_BTN_A   9    // left button:   cycle icons / next
#define PIN_BTN_B   8    // middle button: confirm
#define PIN_BTN_C   7    // right button:  cancel / back
#define PIN_BUZZER  6    // piezo buzzer (passive) to GND
#define PIN_LED     13   // on-board LED blinks while the pet is calling

// 1 = each button wired between its pin and GND; the internal pull-ups are used,
//     so no resistors are needed. (Recommended.)
// 0 = original Tamaguino wiring: button between pin and 5V plus a 10k pull-down
//     resistor from the pin to GND.
#define BUTTONS_ACTIVE_LOW 1

#define OLED_ADDR 0x3C   // most 128x64 I2C SSD1306 modules; some use 0x3D

// ---- A-Com: battle a real Digital Monster (original / 20th) ----
// Circuit (same as the DMComm "A-Com"): PIN_ACOM_OUT -> R1 3.9k -> toy's signal prong;
// the signal prong also goes to PIN_ACOM_IN and through R2 5.6k to GND; toy GND -> GND.
// Never connect the toy straight to an output pin: the divider keeps it under ~3 V.
#define ACOM_ENABLED   1
#define PIN_ACOM_OUT   A2
#define PIN_ACOM_IN    A3
#define ACOM_VERSION   0      // which original model BitBeast claims to be: 0 = Ver.1 ... 4 = Ver.5
#define ACOM_MAX_BOOST 4      // effort hearts sent as the "boost" (V.S. energy); 2 for early US units
#define ACOM_LISTEN_S  30     // "Link: wait" gives up after this many seconds

// ---- Gameplay tuning ----
#define CALL_TIMEOUT_MIN      10    // ignoring a call this many game-minutes = 1 care mistake
#define STARVE_DEATH_MIN      720   // hunger stuck at 0 this long (awake) -> dies
#define SICK_DEATH_MIN        360   // sick / injured and untreated this long -> dies
#define MAX_INJURIES          20    // lifetime sicknesses + injuries -> dies
#define INJURY_CHANCE_WIN     10    // % chance of injury after winning a battle
#define INJURY_CHANCE_LOSE    30    // % chance of injury after losing a battle
// Evolution requirements live in the EVO_RULES table in species.h
// (edit visually with tools/evolution-tool.html).

// ---- System ----
#define FRAME_MS          50          // ~20 fps
#define SAVE_INTERVAL_MS  300000UL    // autosave every 5 min (EEPROM slots rotate for wear-levelling)
#define DIM_AFTER_MS      60000UL     // dim the OLED after 1 min without input (0 = never)
#define SCREEN_OFF_MS     300000UL    // blank the OLED after 5 min idle on the home screen (0 = never)
#define DEBOUNCE_MS       25
