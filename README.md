# BitBeast: a virtual-monster pet for Arduino UNO

BitBeast is a virtual pet in the style of the classic 90s fighting-monster pets. You hatch
an egg, feed it and train it, battle with it, keep its area clean and put it to bed. How
well you look after it decides what it evolves into. It runs on an **Arduino UNO** with a
**128x64 SSD1306 I2C OLED**, three buttons and a piezo buzzer. This is the same hardware
as [Tamaguino](https://github.com/alojzjakob/Tamaguino), which this project grew out of.
Tamaguino is cloned in `tamaguino/` for reference.

The creatures, their names and the art are all original. The game copies the *mechanics*
of the genre, not any particular franchise.

```
vpet/
├── BitBeast/            <- open this folder in the Arduino IDE
│   ├── BitBeast.ino     game logic
│   ├── config.h         pins, button wiring, tuning knobs
│   ├── types.h          data structures
│   ├── species.h        creature table + evolution rules (edit with tools/evolution-tool.html)
│   ├── sprites.h        GENERATED from tools/sprites.txt
│   └── src/Adafruit_SSD1306/  bundled OLED driver, trimmed: no logo, no SPI, no rotation, fixed buffer
├── tools/
│   ├── sprites.txt      all pixel art as editable ASCII
│   ├── gen_sprites.py   sprites.txt -> sprites.h (+ preview PNG)
│   ├── sprite-tool.html browser sprite editor / converter (txt <-> .h <-> images)
│   ├── evolution-tool.html  visual species + evolution-tree editor (writes species.h)
│   └── sprite_preview.png
└── tamaguino/           upstream Tamaguino (reference only)
```

## Hardware

| Part | UNO pin |
|---|---|
| OLED SDA | A4 |
| OLED SCL | A5 |
| OLED VCC / GND | 5V (or 3.3V, check your module) / GND |
| Button A (next) | D9 |
| Button B (confirm) | D8 |
| Button C (back) | D7 |
| Piezo buzzer (+) | D6 (buzzer − to GND, a passive buzzer sounds best) |
| Call LED | D13 (the on-board LED, nothing to wire) |

**Button wiring.** By default each button goes **between its pin and GND**, with no
resistors (the code turns on the internal pull-ups). If you already built a Tamaguino with
buttons to 5V and 10k pull-down resistors, set `BUTTONS_ACTIVE_LOW 0` in `config.h`.

```
 D9 ──[A]── GND      D8 ──[B]── GND      D7 ──[C]── GND      D6 ──(+ buzzer −)── GND
```

Some OLED modules use I2C address `0x3D` instead of `0x3C`. If the screen stays blank and
the on-board LED blinks fast, change `OLED_ADDR` in `config.h`.

## Building and flashing

1. Install the **Arduino IDE** (2.x).
2. Library Manager: install **Adafruit GFX Library**, and accept its dependency **Adafruit
   BusIO**. The SSD1306 driver itself is bundled in `BitBeast/src/` with its unused Adafruit
   logo removed, which frees about 1.5 KB of flash. An installed Adafruit SSD1306 library is
   harmless; the sketch doesn't use it.
3. Open `BitBeast/BitBeast.ino`, choose **Board: Arduino Uno**, pick the port, and click Upload.

Or from the command line with `arduino-cli`:

```bash
arduino-cli core install arduino:avr
```
```bash
arduino-cli lib install "Adafruit GFX Library"
```
```bash
arduino-cli compile --fqbn arduino:avr:uno BitBeast
```
```bash
arduino-cli upload -p COM3 --fqbn arduino:avr:uno BitBeast
```

**Kiosk / screen-test mode.** `config.h` has `#define KIOSK_MODE 1`, and it currently
ships set to **1**. In this mode there is no game:

1. A screen test runs: all pixels on, then a checkerboard, then a border with
   "SCREEN TEST OK".
2. Random creatures then cycle forever, 6 s each (`KIOSK_SHOW_MS`). Each one walks around
   with its idle and action frames, with its name above and stage below.

Only the OLED needs to be connected. Nothing is read from or written to EEPROM, so a pet
you already saved survives. If button B is wired, it skips to the next creature. Set
`KIOSK_MODE` to **0** for the normal game.

Kiosk mode builds to 18.6 KB, because the unused game code is left out. Normal mode
builds to 30.3 KB.

**Memory budget.** The screen's 1 KB frame buffer is a fixed array (see the bundled driver
below), so the IDE's "Global variables use X bytes" figure is the real RAM total. Keep at
least ~400 bytes free for the stack.

**Bundled, trimmed screen driver.** `BitBeast/src/Adafruit_SSD1306/` is Adafruit's SSD1306
driver 2.5.17 with four switches turned on at the top of its header. Together they save
about 3.7 KB of flash:
- `SSD1306_NO_SPLASH`: drops the unused Adafruit logo.
- `SSD1306_NO_SPI`: I2C screens only.
- `SSD1306_NO_ROTATION`: `setRotation()` is ignored.
- `SSD1306_STATIC_BUFFER`: fixed buffer instead of `malloc()`.

Comment a switch out to get that feature back. The sketch also uses direct port access
instead of `digitalRead()`/`digitalWrite()`, and a small built-in random generator, so it
targets the UNO's ATmega328P specifically.

## Controls

| | Home screen | Menus | Mini-games |
|---|---|---|---|
| **A** | select next icon | next option | – |
| **B** | use icon (with no icon selected: show the clock) | confirm | fire / stop the gauge |
| **C** | deselect | back | quit training |

**Screen layout.** The top 16 pixel rows hold the menu, titles and status: all 8 icons
in one row, or a screen's title, round counters and HP. The 48 rows below are the
creature's world. On the common two-color modules (yellow top band, blue below), the menu
is yellow and the pet is blue. On single-color modules it looks the same, just in one
color.

The eight icons, left to right:

| Icon | Does |
|---|---|
| 📊 Stats | name, stage, age, weight → hunger → strength → effort → battle record → care log |
| 🍖 Feed | **Meat** = +1 hunger heart, +1 g. **Vitamin** = +1 strength heart, +2 g |
| 🏋 Train | timing mini-game, see below |
| ⚔ Battle | fight a random wild monster (Rookie and above) |
| 💧 Flush | clean up the poop |
| 💡 Lights | turn the lights off when it sleeps |
| ✚ Heal | medicine for sickness or injury (sometimes needs 2 doses) |
| 🕐 Setup | set clock · sound on/off · game speed · start a new egg |

A blinking **`!`** at the top right (plus beeps and the D13 LED) means the pet is
**calling**. It is hungry (0 hunger hearts), weak (0 strength hearts), or asleep with the
lights on. A call ignored for 10 game-minutes counts as a **care mistake**. Any button press
stops the repeat beeps. The call stays on screen until you fix the cause.

## Care guide

- **Hunger and strength** drain over time while it's awake, at a different rate per
  species. Both are shown as 4 hearts.
- **Poop** builds up. With 4 poops on screen it gets **sick** (skull icon).
- **Overfeeding.** It eats one extra piece of meat past full and then refuses more. Each
  overfeed has a small chance of making it sick.
- **Sleep.** From In-Training onwards each species has a bedtime. Turn the **lights off**
  when it falls asleep. The lights come back on by themselves in the morning. While it
  sleeps, its needs stop draining.
- **Training.** A cursor sweeps across a gauge. Press **B** to stop it inside the solid
  target zone. There are 5 rounds, and the zone gets smaller and the cursor faster each
  round. 3 or more hits gives +1 strength heart. Every training session counts toward
  evolution, and every 4 sessions add an **effort** heart. Each session costs 2 g.
- **Battles.** Each side has 3 hit points and both sides take turns shooting. Your hit
  chance depends on species power, strength hearts, effort hearts, and whether it is
  overweight. Each battle costs 4 g. After a battle it may be **injured** (10% if it won,
  30% if it lost). Injured or sick pets refuse to train or battle.
- **Death.** It dies if any of these happen:
  - it starves (0 hunger for 12 h awake)
  - it is left sick or injured for 6 h
  - it reaches 20 lifetime sicknesses and injuries
  - it reaches old age (Champions 15 days, Grubble 12, Ultimates 20)

  Press B on the grave to start the next generation.

## Evolution

| Stage | Time in stage | Creatures |
|---|---|---|
| Egg | 2 min | Egg |
| Fresh | 10 min | Blipp |
| In-Training | 6 h | Squeek |
| Rookie | 24 h | Embrix · Mossup |
| Champion | 36 h (then waits until the condition is met) | Pyrosaur · Glimwing · Ironhorn · Thornox · Tidefin · Grubble |
| Ultimate | – | Solarex · Abyssor |

Care mistakes and training counts **reset at every evolution**, so each stage is scored on
its own:

```
Squeek ── 0-2 mistakes ──> Embrix ─┬─ 0-3 mistakes, 16+ trainings ──> Pyrosaur ─┐
       └─ 3+  mistakes ──> Mossup  │  0-3 mistakes, <16 trainings ──> Glimwing ─┼─> Solarex
                             │     ├─ 4+  mistakes, 5+ trainings  ──> Ironhorn ─┘
                             │     └─ 4+  mistakes, <5 trainings  ──> Grubble ──┐
                             ├─ 0-3 mistakes, 16+ trainings ──> Thornox ────────┼─> Abyssor
                             ├─ 0-3 mistakes, <16 trainings ──> Tidefin ────────┘
                             └─ (4+ mistakes: Ironhorn / Grubble as above)

Champion -> Ultimate: 15+ battles with an 80%+ win rate, fought as that Champion.
```

It won't evolve while asleep, sick or injured. It waits until it is awake and healthy.

## Time, clock and saving

- The pet keeps its own clock. You set it on first boot, and later under Setup → Set clock.
- **Time only passes while it's powered.** The UNO has no real-time clock, so unplugging it
  freezes the pet. Nothing bad happens while it's off.
- The game **autosaves to EEPROM** every 5 minutes, and immediately on evolution, death and
  settings changes. Saves rotate across 16 EEPROM slots, so the EEPROM lasts for decades.
  If you unplug it, you lose at most 5 minutes.
- **Game speed** (Setup) can be x1, x10, x60 or x600. Use x600 to watch a whole life cycle
  in minutes when testing. Hatching then takes 0.2 s, and one in-game day takes 2.4 minutes.
- The screen dims after 1 minute idle and blanks after 5 minutes on the home screen. Any
  button wakes it (that press is not used as input). A call or an evolution also wakes it.

## Changing the game

- **Art.** Edit `tools/sprites.txt` (`#` = pixel on). Then run
  `python tools/gen_sprites.py`, which regenerates `BitBeast/sprites.h` and writes
  `tools/sprite_preview.png`. Creatures are 16x16, drawn at 2x, and the art must face **left** (the game mirrors it to face right when needed).
- **Sprite tool.** Open `tools/sprite-tool.html` in a browser (double-click it; it needs
  no server or internet). It can:
  - open or paste `sprites.txt` or `sprites.h`. Any C byte arrays import too, Tamaguino's
    included.
  - import images, either resized to one sprite or sliced from a sheet, with a threshold
    slider and an invert option
  - edit pixels with a paint editor: flip, shift, invert, resize, duplicate, reorder, undo
  - preview `NAME0`/`NAME1` pairs animating on a mock 128x64 OLED
  - save `sprites.txt`, `sprites.h` or PNGs

  Its `sprites.h` output is byte-identical to `gen_sprites.py`'s, and its `sprites.txt`
  output keeps your comments. Chrome and Edge let you overwrite the files in place; other
  browsers download them instead.
- **Remembered folder.** In Chrome and Edge, both tools' Open and Save dialogs start in the
  folder of the last file you opened or saved, in either tool. This carries over to your
  next visit.
- **Pick one art source.** `gen_sprites.py` rebuilds `sprites.h` from `sprites.txt`. If you
  save `sprites.h` straight from the Sprite Tool, also save `sprites.txt`. Otherwise the
  next `gen_sprites.py` run overwrites your new art with the old text file.
- **Creatures and evolution.** All of this lives in `BitBeast/species.h`:
  - the `SPECIES` table: hunger, strength and poop rates, bedtime, battle power, weight,
    evolution time, lifespan and sprite frames for each creature
  - the `EVO_RULES` table: evolution conditions (care-mistake range, training range,
    minimum battles, minimum win %, weight range)

  When a species reaches its evolution time, the rules for it are tried in order, and the
  first one whose conditions all match picks the next form. If none match, it keeps
  checking every minute; that's how Champions wait for their battle record. Species 0 is
  the egg every generation starts from. Names can be up to 11 letters. The large-letter
  name on the Stats screen fits 10, so an 11-letter name is shown at normal size there. Each species costs 25 bytes of flash and each rule
  10 bytes, plus the sprites.
- **Evolution tool.** Edit both tables visually with `tools/evolution-tool.html`:
  - Open `BitBeast/species.h`, then load `sprites.h` (or use the Sprite Tool's sprites).
    Species appear as animated cards, in columns by stage, with arrows for the rules.
  - Drag from a card's right-hand dot onto another card to add a rule. Click a card to
    edit its stats and sprite frames, or to reorder its rules. Click an arrow to edit that
    rule's conditions.
  - **What-if** takes care mistakes, trainings, battles, wins and weight, and highlights
    the line an egg raised that way follows.
  - **Problems** flags species that can never evolve, can get stuck, or can't be reached,
    rules that can never fire, missing sprites, and bad names.
  - **Save species.h** writes the file the firmware compiles.

  If you run `python -m http.server` in `vpet/` and open
  `http://localhost:8000/tools/evolution-tool.html`, it loads the project files by itself.
  Renaming or deleting species changes their numbers, so start a new egg after flashing.
- **Difficulty.** The timeouts, death limits and injury odds are in `config.h`.

## Status and ideas

The firmware compiles cleanly for the UNO (arduino:avr 1.8.8, Adafruit SSD1306 2.5.17,
Adafruit GFX 1.13.0):

- **Flash (game mode):** 26,434 of 32,256 bytes (82%), leaving about 5.8 KB for new
  features and species. Kiosk mode is 15,152 bytes.
- **RAM (game mode):** 1,543 of 2,048 bytes. The screen's 1 KB frame buffer is now a fixed
  array, so this figure is the real total, and about 500 bytes are left for the stack.
- **RAM:** 528 bytes of globals, plus the 1 KB frame buffer at runtime, leaves about 500
  bytes of stack.

**It has not been tested on real hardware yet.**

A local copy of `arduino-cli` is in `tools/arduino-cli/`. To compile with it:

```bash
tools/arduino-cli/arduino-cli.exe compile --fqbn arduino:avr:uno BitBeast
```

Possible next steps that still fit on an UNO:

- link battles between two UNOs over a 1-wire serial cable
- a second training mini-game
- more species and branches
- an RTC module (DS3231) so time keeps running while unplugged

## License

GPLv3, the same as Tamaguino (see `LICENSE`). Based on Tamaguino by Alojz Jakob.
