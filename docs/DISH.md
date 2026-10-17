# Pocket Dish (the `dish` branch)

A Tamagotchi-style petri dish on the tank's four boards: the keeper raises a
strain of germs, tweaks the dish (heat, pH, salt, light, UV, medicine) and
watches it evolve. The tank on `main` is untouched; nothing here ships over
OTA until it is decided to.

## Where it lives

| | |
|---|---|
| `common/dish.c` | the world: cells, fields, genomes, division, phages, the spore vault, the save, time away |
| `common/dish_brain.c` | each strain's strategy (rules for now) and the state line a distilled model will read |
| `common/dish_render.c` | the dark-field microscope; renders in bands, one per core |
| `common/dish_ui.c` | needs, the icon arc, the pages, gestures, the first-run setup, the sound queue |
| `common/dish_prog.c` | DNA, badges, the Microbe-dex, unlocks |
| `sim/dish_main.c` | the simulator: selftests, `--snapshot`, `--bench`, a window when SDL2 is installed |
| `firmware/main/dish_app.c` | the board: built instead of `main.c` when `CONFIG_POCKET_DISH` |

## Run it

```bash
make -C sim dish-check-all
```

That runs the five dish selftests (dish, pet, evolve, prog, render) on all four glasses.

```bash
cd sim && make dish && ./dishsim --snapshot /tmp/dish-
```

That writes PPM frames of every page. With SDL2 installed (`brew install sdl2`),
`make dish` also builds the window: the mouse is the finger. Keys:
N light, O outbreak, F 10x time, D +50 DNA, S save, X new dish, Q quit.

## Build it for a board

The board's own overlay first, then `sdkconfig.dish`, in its own build directory:

```bash
cd firmware && idf.py -B build-dish-round -DSDKCONFIG=build-dish-round/sdkconfig "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.round;sdkconfig.dish" build
```

The `dish` workflow (`.github/workflows/dish.yml`) builds all four, plus the tank's 1.8 image, on every push to `dish`.

## Not yet checked on a board

- Frame rate. On a Mac a busy dish (176 cells) renders in about 0.5 ms per band.
  The 25 fps target on the S3 is an estimate, not a measurement.
  `dish_app.c` logs the fps and every stage's time every 10 s.
- Touch through `touch_port_read_raw`. The round board's lift rule
  (touch_port_poll) is not applied to it.
- The night is a PMIC power-off only: a short PWR press saves and powers
  off. A board without an RTC (the round 1.75C) resumes without living the
  time away. Light sleep, the drowse and update mode are the tank's for now.
- Sounds reuse the tank's bank at other pitches. New cues (pop, divide,
  burst, drip, zap) go through `tools/make_sounds.py` like the tank's.
