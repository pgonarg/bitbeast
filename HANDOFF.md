# BitBeast: session handoff

Read this first in a new session. It records what exists, how it's built, the decisions
behind it, and what's next. Last updated 2026-11-12. `README.md` is the user-facing manual;
this file is the working notes.

## Goal

A fully featured V-pet in the style of the 1997 Digital Monster, running on an
**Arduino UNO + 128x64 SSD1306 I2C OLED**, with:

1. the complete game playable on the device
2. an **A-Com** link for battling the user's real **Digital Monster original / 20th**
   V-pet
3. room left in flash for adding more monsters

The project started from a clone of Tamaguino (`tamaguino/`, reference only; GPLv3, so
BitBeast is GPLv3 too).

**Repository:** https://github.com/pgonarg/bitbeast (private), branch `main`, pushed
2026-11-12. `.gitignore` leaves out `tools/arduino-cli/`, `tamaguino/` and `.claude/`.
Pushing works through Git Credential Manager (browser sign-in), so run `git push` in the
user's Terminal panel if it needs to sign in again. Commit or push only when the user asks.

## Hardware status

| Item | State |
|---|---|
| Board | UNO clone with a **CH340** USB chip, on **COM5**. COM3/COM4 are Bluetooth ports: never upload there |
| Screen | 128x64 SSD1306 I2C at 0x3C, **two-color**: rows 0-15 yellow, rows 16-63 blue. SDA→A4, SCL→A5. Works |
| Buttons | **Not wired yet.** Planned: A=D9 (next), B=D8 (confirm), C=D7 (back), each to GND (internal pull-ups, `BUTTONS_ACTIVE_LOW 1`) |
| Buzzer | **Not wired yet.** Code uses D6. Moving it to **D3 or D11** is recommended: those pins let a future lean buzzer driver use hardware toggling and save about 1.1 KB |
| LED | On-board D13 blinks while the pet is calling |
| A-Com circuit | Not built. Code is done (see "A-Com" below): A2 → R1 3.9k → signal prong; signal → A3; signal → R2 5.6k → GND; toy GND → GND |

The board currently runs the **kiosk build** (screen test, then random creatures walking or
attacking). The **game mode has never run on hardware**: without buttons it can't get past
the first-boot Set Clock screen.

## Building and uploading

A local `arduino-cli` 1.6.0 is in `tools/arduino-cli/`. The AVR core 1.8.8, Adafruit GFX
1.13.0 and BusIO are installed in the standard folders (`%LOCALAPPDATA%\Arduino15`,
`Documents\Arduino\libraries`), which Arduino IDE 2 shares.

```bash
# game build (overrides KIOSK_MODE from the command line), compile + upload
tools/arduino-cli/arduino-cli.exe compile --fqbn arduino:avr:uno --build-path <scratch>/b-game --build-property "compiler.cpp.extra_flags=-DKIOSK_MODE=0" BitBeast
tools/arduino-cli/arduino-cli.exe upload --fqbn arduino:avr:uno --input-dir <scratch>/b-game -p COM5 --verify
# kiosk build (config.h currently has KIOSK_MODE 1)
tools/arduino-cli/arduino-cli.exe compile --fqbn arduino:avr:uno --build-path <scratch>/b-kiosk BitBeast
```

Gotchas:
- **Always pass `--build-path`** (a scratch folder). Changing build flags with the default
  cache fails with "too many levels of symbolic links".
- `upload` prints nothing on success unless `-v`. Use `--verify -v` and look for
  "N bytes of flash verified".
- The user can also upload from **Arduino IDE 2**: open `BitBeast/BitBeast.ino`, choose
  Board: Arduino Uno and Port: COM5. The IDE can't pass flags, so set `KIOSK_MODE` in
  `config.h`. The IDE reloads files changed on disk if its tab has no unsaved edits.
- Always ask the user before uploading to their board.

## Sizes (2026-11-12, with the A-Com)

| Build | Flash (of 32,256) | RAM (of 2,048) |
|---|---|---|
| Game (`KIOSK_MODE 0`) | **28,320 (87%)**, 3,936 free | 1,551, about 497 left for the stack |
| Kiosk | 15,174 | 1,486 |

The A-Com cost 1,886 B of flash and 8 B of RAM (before it: 26,434 / 1,543).

The RAM figure is honest: the 1 KB frame buffer is now a static array, not `malloc`.
Rough costs: a species is about 100 B (two 16x16 frames = 64 B, a table row = 26 B, plus
rules), and an evolution rule is 10 B.

Game-build flash breakdown measured before the trims (DWARF line-table attribution; LTO
inlining makes `nm` symbol names useless):

| Source | Bytes |
|---|---|
| sketch code + data | 14.5 K |
| Adafruit GFX (incl. 1,280 B font) | 4.9 K |
| SSD1306 | 3.5 K (since cut by about 2.3 K) |
| Wire | 2.0 K |
| `tone()` | 1.3 K |
| libgcc math | 1.1 K |
| malloc/free | 0.6 K (gone) |
| random | 0.3 K (gone) |

## Code map (`BitBeast/`)

| File | Contents |
|---|---|
| `BitBeast.ino` | Everything else: drawing helpers, sound player, save/load, life cycle, `gameMinute()`, input, every screen mode (`modeXxx`), kiosk code (`#if KIOSK_MODE`), `setup()` and `loop()` |
| `config.h` | `KIOSK_MODE` (overridable with `-D`), `KIOSK_SHOW_MS`, pins, `BUTTONS_ACTIVE_LOW`, `OLED_ADDR`, gameplay tuning (call timeout, death limits, injury odds), timing (frame, autosave, dim, screen off) |
| `types.h` | `Species` struct (`name[12]`, so up to 11 letters; `acomSlot`), `EvoRule`, `PetState` (40 B, saved to EEPROM), `SaveBlock`, `Mode` enum, `F_*` flags, `NOSLEEP`, `ANY` |
| `species.h` | The `SpeciesId` enum, the `SPECIES[]` table, the `EVO_RULES[]` table and `STAGE_NAMES`. Written by the evolution tool |
| `sprites.h` | Bitmaps. Written by the Sprite Tool or `gen_sprites.py` |
| `src/Adafruit_SSD1306/` | Bundled Adafruit SSD1306 2.5.17, trimmed by four switches in its header: `SSD1306_NO_SPLASH`, `SSD1306_NO_SPI`, `SSD1306_NO_ROTATION`, `SSD1306_STATIC_BUFFER 1024`. In the .cpp, `if (wire)`/`if (spi)`/`switch (rotation)` were routed through the macros `SSD1306_USE_WIRE`/`SSD1306_USE_SPI`/`SSD1306_ROT` so the dead branches compile away |

### Key design decisions and conventions

- **Data-driven evolution.** `chooseEvolution()` walks `EVO_RULES` in order and the first
  match wins. Conditions are per-stage counters (care mistakes, trainings, battles, win %,
  weight) that reset at every evolution. No match means the pet keeps checking every
  minute (that's how Champions wait for a battle record). **Species 0 is always the
  starting egg.** The firmware never hard-codes `SP_xxx` names; it uses only `SP_COUNT`
  and `SP_NONE`.
- **Sprites face LEFT.** `drawMon(id, frame, x, y, faceRight)`: `faceRight` mirrors. Frame 0
  is idle, frame 1 is the action pose. Creatures are 16x16 drawn at 2x. `petLeft` means
  facing or walking left.
- **The `SHOT` sprite points left** too; `drawShot(x, y, right)` mirrors it.
- **Attack launch point** comes from `mouthOf(id)`: the leftmost column of the pixels that
  differ between frames 0 and 1, or failing that the leftmost lit column of frame 1.
  `mouthScreenX`/`mouthScreenY` convert it to screen coordinates. Used by kiosk, training
  and battle.
- **Two-color layout.** Everything in rows 0-15 (yellow) is menu, title or status: all 8 icons
  in one row at x = i*16+4, y=4. Rows 16-63 (blue) are the world: pet at y≈24-28, poop at the
  right, "!" call alert at (122,18).
- **The UNO does 16-bit `int` math.** Cast to 32-bit before multiplying times by pixel
  distances. This caused a real bug: the kiosk shot jumped back onto the creature.
- **Direct port I/O.** The `FAST_READ`/`FAST_WRITE`/`FAST_INPUT`/... macros work for UNO pins
  D0-D13 and A0-A5 only. A `static_assert` guards the pin numbers.
- **`rnd(n)` / `rndRange(a, b)` / `rndSeed()`** is a 16-bit xorshift generator that replaced
  Arduino `random()`. Don't reintroduce `random()`.
- **Saving.** 16 rotating 64-byte EEPROM slots, each with a magic number (`SAVE_MAGIC
  0xB17C`), a sequence number and a CRC. Autosave every 5 minutes, plus immediately on
  evolution, death and settings changes. **Changing `PetState` requires bumping
  `SAVE_MAGIC`.** Changing species order or count changes the meaning of a saved species
  number, so start a new egg after flashing.
- **Time only passes while powered** (no RTC). Game speed can be x1/x10/x60/x600.
- The **linker drops unused functions**, so kiosk and game share code with no size penalty.
  The user asked about this and is happy with the single sketch and the `KIOSK_MODE` switch.

### Game features already implemented

- **Life cycle:** egg hatching; Fresh/In-Training/Rookie/Champion/Ultimate.
- **Care:** hunger and strength hearts, effort (4 trainings = 1 heart), weight, overfeeding
  (one extra meat, a 1-in-6 chance of sickness, then refusal), poop (4 poops = sick),
  sickness and injury with medicine (1-2 doses).
- **Sleep:** sleep and wake hours per species; you must turn the lights off. Lights on while
  asleep counts as a call.
- **Calls:** hunger 0, strength 0, or lights left on. 10 minutes ignored = 1 care mistake.
  Repeat beep every 60 s until any button is pressed.
- **Training:** a timing gauge, 5 rounds; 3 or more hits = +1 strength.
- **Battles:** against random computer opponents of similar stage. Hit chance comes from
  power, strength and effort, minus a penalty when overweight. 3 HP each. Injury chance is
  10% on a win, 30% on a loss. The win record feeds the evolution rules.
- **Death:** starvation (12 h), untreated illness (6 h), 20 injuries, or species lifespan.
  Then a grave and a new generation.
- **Screens and system:** stats pages, clock view and setting, setup menu (clock, sound,
  speed, new egg), screen dim after 1 minute and off after 5 minutes.
- **A-Com link battles** (`#if ACOM_ENABLED` section in `BitBeast.ino`, `modeLink` = the
  Battle menu: vs CPU / Link: go 1st / Link: go 2nd). **Written, compiled, never run on
  hardware.** See "A-Com" below.
- **Kiosk mode:** a screen test, then a random creature for 6 s (B skips). Half the time it
  walks; half the time it stands at an edge and fires shots from its mouth.

## The user's content (they edit these)

- They renamed species after real Digimon: Egg, Botamon, Koromon, Agumon, Greymon,
  MGreymonV, Mamemon, Monzaemon, Kabuterimon, Angemon, Numemon, Vademon, Digitamamon (13
  in all). The sprite identifiers still use the original names (`SPR_EMBRIX0`, ...).
- Their `species.h` currently has **only 4 evolution rules**, so most species never evolve
  yet. The evolution tool's Problems list shows the gaps.
- They redrew the sprites and saved **`sprites.h` directly**. `POOP` was split into
  `POOP0`/`POOP1` (the code animates them), and `SHOT` is now a left-pointing `<<`.
- **`tools/sprites.txt` is stale** (50 sprites against 51 in `sprites.h`). Running
  `gen_sprites.py` would overwrite their art. They should re-save `sprites.txt` from the
  Sprite Tool, or treat `sprites.h` as the source.

## Tools (`tools/`)

- `sprite-tool.html`: sprite editor and converter (sprites.txt ↔ sprites.h ↔ images),
  with a paint editor and an OLED preview. Its sprites.h output is byte-identical to
  `gen_sprites.py`'s.
- `evolution-tool.html`: visual species and evolution-rule editor. It reads and writes
  `species.h` byte-identically, and has a what-if simulator, a problems list and drag to
  connect. Names are limited to 11 characters.
- Both tools remember the last folder (File System Access API, Chrome/Edge; the handle is
  stored in IndexedDB `bitbeast-tools`, picker id `bitbeast`). Drafts are kept in
  localStorage.
- `gen_sprites.py`: sprites.txt → sprites.h plus `sprite_preview.png`.
- `.claude/launch.json`: a `python -m http.server 8765` config used to test the tools in the
  browser pane. When served, the evolution tool auto-loads the project files.

## Roadmap (agreed with the user)

1. **Wire the buttons and buzzer** (buzzer preferably on D3 or D11; change `PIN_BUZZER`).
   Upload the **game build** and play-test on the device; fix what comes up. Nothing else
   has been verified in game mode.
2. **A-Com hardware:** build the circuit and validate it with the stock **DMComm** Arduino
   sketch, as a separate upload, to see the real V-pet's packets over serial.
3. ~~A-Com in BitBeast~~ **done in code 2026-11-12**, untested. After step 2, test both
   "go 1st" and "go 2nd" against the user's toy, and find out whether the 20th needs a
   special mode for original battles.
4. **Remaining flash trims** when more room is needed for species (about 5.6 KB):
   - replace Adafruit GFX with a lean in-sketch renderer, trimming the font to ASCII 32-126
     and dropping the always-linked line/rotation functions and custom-font code: ~3 KB
   - send-only I2C instead of Wire: ~1.5 KB flash and ~160 B RAM
   - lean buzzer driver instead of `tone()`: ~1.1 KB; also lets `pinMode`/`digitalWrite`
     drop out
5. Then: more species and rules. Optional: BitBeast-vs-BitBeast battles (same protocol), a
   DS3231 RTC, battery power.

Budget check: 3.9 KB free now (after the A-Com), room for about 35 more species at ~100 B
each, fewer with art-heavy ones. With the step 4 trims, about 90.

## A-Com (implemented 2026-11-12, not yet verified on hardware)

How the code works (`BitBeast.ino`, section "A-Com link battle"):
- **Electrical.** `PIN_ACOM_OUT` (A2) is always an output: HIGH = idle (the divider holds the
  line at ~2.9 V), LOW = active (pulls the line low through R1). The line is read with
  `analogRead(A3)`; below 389 (1.9 V) = low. A digital read can't be used: 2.9 V is under
  the 328P's 3.0 V logic-high threshold. This mirrors DMComm's `DComOutput(A2, A1)` +
  `AnalogProngInput(A3)`; the notOE pin (A1) is not needed on an A-Com.
- **Timings** are DMComm's `dmcomm_classic_communicator.cpp` values for signal type V
  (send: 59 ms low, start 2083/917 µs, bit 1 = 2667/1667, bit 0 = 850/3167, then 400 µs idle;
  receive windows and the 1800 µs bit threshold copied too; reply timeout 100 ms).
- **Packets** (dmcomm.github.io/orig_battle, Humulos wiki "DMOG DigiROM format"): low byte =
  two nibbles, high byte = its bitwise NOT. P1 = `(boost << 4) | slot`, P2 = `(version << 4)
  | outcome`, outcome 1 = win / 2 = loss **from the sender's side**. Order: first's P1,
  second's P1, first's P2, second's P2. The first player decides. Example `FC03-FD02` =
  Agumon (slot 3), boost 0, then version 0 and "I lost".
- **Going 1st:** `acomRun(true)` sends P1 once a second until the toy answers (C cancels,
  `ACOM_LISTEN_S` timeout), rolls the result with the Ver.1 odds table `ACOM_ODDS` (Humulos,
  16ths; boost difference adds 1/16 per level, a guess; overweight -2), sends P2, and
  requires the toy's P2 reply.
- **Going 2nd:** listens in 200 ms windows (checking C between them), replies P1, reads the
  toy's P2, and replies P2 with the opposite outcome and our `ACOM_VERSION` (a real V1 would
  echo the opponent's version instead).
- Then `beginBattle()` plays the normal battle animation with `linkFight` set: `nextShot()`
  never lets the loser land the final hit, so the animation ends as agreed, and
  `finishBattle()` records it like any battle.
- **Species → slot:** `Species.acomSlot` (3-14 = A-L, 0 = by stage: Rookie 3, Champion 7,
  Ultimate 12). The user's table maps Agumon 3, Greymon 5, Numemon 11, MGreymonV 12,
  Mamemon 13, Monzaemon 14 after the real Ver.1 chart. Mamemon is a Champion in BitBeast
  but slot K is a Perfect, so it fights far above its stage over the link; the user may
  want to change it. The opponent is drawn as the BitBeast species with the same slot,
  else the first one of that stage.
- **Evolution tool** reads/writes the new column (optional when reading, so old files still
  load) and has an "A-Com slot" field. A draft saved before the column existed is set aside
  under `bitbeast-evolution-old` so it can't save every slot as 0.

Open questions for the hardware test: the 20th's mode for original battles; whether the
receive windows need widening because `analogRead` takes ~112 µs per sample; and whether a
real toy accepts version 0 against itself.

### Research notes (from the DMComm docs, before implementing)

- DMComm (by BladeSabre) is MIT licensed: https://github.com/dmcomm/dmcomm-project,
  https://github.com/dmcomm/dmcomm-arduino-lib. The classic A-Com uses an Arduino Nano,
  which has the same ATmega328P as the UNO.
- **Circuit** (https://dmcomm.github.io/guide/nano): a voltage divider R1 = **3.9 kΩ**,
  R2 = **5.6 kΩ** (similar values are OK). The toy's signal prong goes to **A3**, and the
  toy's GND prong to GND. **Never drive 5 V straight into the toy.** The divider sets
  the ~2.7-3.0 V level. Check the exact divider wiring in the guide before building.
- **Bus** (https://dmcomm.github.io/electrical): idles high around 2.6-2.8 V. The V-pet has
  a ~100 kΩ weak pull-up, ~3 kΩ strong drive and ~3 kΩ pull-down. The line is pulled low
  64 ms before the first packet. **Pulse-width bits, ~4.3 ms each:**
  - start bit: ~2.0 ms high, 0.9 ms low
  - "1": ~2.7 ms high, 1.6 ms low
  - "0": ~1.0 ms high, 3.3 ms low

  Packets are 16 bits, sent LSB first, and the devices take turns (the V-pet replied about
  18.6 ms after the ACom). Loading the line too much (e.g. 47 kΩ to ground) makes the V-pet
  refuse to send.
- **Supported:** DigiROM "V1" original battle (also the original Pendulum, and the "other"
  battle mode on newer devices). **Still to look up:** the meaning of each battle packet
  (creature index, power, how the winner is decided) on Humulos's wiki
  (https://wiki.humulos.com/wiki/Communicators) and in the DMComm docs. Also confirm which
  battle mode the **20th** needs.
- The user owns an **original / 20th** model. NFC devices such as the Vital Bracelet are
  incompatible.
