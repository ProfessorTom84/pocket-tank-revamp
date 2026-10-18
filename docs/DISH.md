# Pocket Dish (the `dish` branch)

A Tamagotchi-style petri dish on the tank's four boards. You raise a strain of germs; every
strain in the dish, yours and every visitor, thinks for itself; and you shape what happens with
light, currents, tilt and evolution. The tank on `main` is untouched, and nothing here ships
over OTA until that is decided.

## The glass is the interface

- **Hold** to shine light. Food blooms under the light. Each strain's mind decides whether to come
  to it or shy away (photo strains love it). This replaces feeding.
- **Drag** to make a current. Use it to herd your colony or to push a threat away.
- **Tilt** the box and everything drifts downhill. On the board this is the QMI8658, with a dead
  zone so a desk that isn't level stays still. In the sim and the browser it is the arrow keys.
- **Tap** a germ to look closer. The lens shows its thought: "BLIP: FLEEING THE VIBRIO
  SURE 82%".
- **Double-tap** to switch the lamp, for day or night. Photosynthesis needs it; night is rest.
- **STORY** (top left) opens the journal and the charts. **DISH** (top right) opens your strain's
  card, with the Microbe-dex, the badges and NEW RUN (tap it twice).

Over each colony floats its thought: an icon for its goal. A dashed line runs to the strain the
thought is about (red for a hunt, yellow for a flight, blue for guarding, pink for a friend).

## How a run goes

- **The start.** Pick a starter. Bacillus, coccus and spirillum are free; cyano, vibrio and myxo
  cost DNA. It starts alone in the dish.
- **Drafts.** Every 2 generations a draft fills the screen and the dish waits until you pick one
  of 3 cards. Only one is ever due: time away grants at most one, and there is no LATER.
  - **The 16 mutations** (`common/dish_mut.c`), each a new thing the cells *do*, with 3 tiers:
    spikes, toxin cloud, zap, jet, engulf, swarm mind, biofilm wall, sunlight, glow lure, spore
    burst, virus tamer, mimicry, scouts, grapple, symbiosis, quorum bloom.
  - **How cards are dealt:** weighted by what the dish is doing to your strain. A strong pressure
    always gets an answer among the three.
  - **What you see:** every mutation shows on the cells, and the build names the form
    ("SPARKING SPINY BACILLUS").
- **Visitors.** A visitor arrives every 3-6 minutes, friendlier early in a run (`dish_prog.c`).
  - **Threats:** phages, myxo, vibrio, mold.
  - **Rivals:** coccus, the acid maker, the halophile.
  - **Friends:** cyano, symbiont, the guardian ciliate, the plasmid donor.
  - **Rules:** no threats come before your first mutation, and visitors leave when their time is
    up. Visitors think too: a vibrio picks off the weakest strain, a ciliate guards your colony,
    a symbiont comes to befriend it.
- **The end of a run.** Until the first mutation, the spore vault always saves the strain. After
  that it saves it once more, and the next extinction ends the run. DNA, badges, the dex and
  unlocks carry over to the next run.

## The minds

Each strain is asked what to do every few seconds and answers `<goal> <target> urgency <d>`.

- **Goals (12):** forage, cluster, explore, rest, divide, spore, seek_light, avoid_light, hunt,
  flee, guard, befriend.
- **Targets:** the other strains in its line.
- **The reflex layer** (`cell_tick`) turns each goal and target into movement and into the
  build's verbs.
- **Who answers:** a small model distilled from gemma4:26b (`common/dish_ai.c`, runbook in
  `docs/dish-ai.md`). Until it is trained, the rules in `common/dish_brain.c` answer with the same
  goals.

## The story

`common/dish_story.c` keeps two records:

- **A log of what happened:** visits and departures, who decided what about whom, mutations,
  records, blooms, zaps, tamed viruses, heavy losses and their causes, comebacks and run ends.
- **A sample of the dish every minute:** your cells, the others, food, births, deaths and their
  cause, the goal, and whether a threat was in.

STORY reads the log as a journal (newest first, drag to scroll). CHARTS shows the population
over time, with mutation marks and red bands while a threat was in, what your cells were lost
to, and how the colony spent its time. Both records are saved with the dish.

## Where it lives

| File | What it holds |
|---|---|
| `common/dish.c` | The world: cells, fields, genomes, division, the verbs, visitors, the spore vault, the save, time away |
| `common/dish_brain.c` | The state line the minds read, its vocabulary, and the rule brain |
| `common/dish_ai.c` | The model's minds: the constrained decode and the need-based scheduler |
| `common/dish_mut.c` | The 16 mutations, the pressures, the draft |
| `common/dish_story.c` | The journal and the minute samples |
| `common/dish_render.c` | The dark-field microscope, the verbs' looks, the thoughts; renders in bands, one per core |
| `common/dish_ui.c` | The gestures, the corner buttons, the pages, the first-run setup, the sound queue |
| `common/dish_prog.c` | DNA, badges, the Microbe-dex, unlocks, the visitor director, drafts coming due |
| `sim/dish_main.c` | The simulator: selftests, `--snapshot`, `--bench`, `--dump-states`, `--ai`, a window when SDL2 is installed |
| `firmware/main/dish_app.c` | The board: built instead of `main.c` when `CONFIG_POCKET_DISH` |

## Run it

```bash
make -C sim dish-check-all
```

That runs the ten dish selftests on all four glasses: dish, pet (the controls), evolve, prog,
render, run (drafts and runs), visitors, verbs (each mutation does its thing), mind (goals
become movement, visitors decide, the story) and ai (the decode on a test model).

```bash
cd sim && make dish && ./dishsim --snapshot /tmp/dish-
```

That writes PPM frames of every page. With SDL2 installed (`brew install sdl2`), `make dish`
also builds the window, where the mouse is the finger and the arrow keys tilt. The other keys:

- **N:** the lamp
- **O:** an outbreak
- **F:** time at 10x
- **D:** +50 DNA
- **S:** save
- **X:** a new dish
- **Q:** quit

## Play it in a browser

The same C code compiled to WebAssembly, with:

- all four board shapes
- the mouse or a finger on the glass, and the arrow keys to tilt
- sound
- a save per board, kept in the browser

It needs Emscripten once (`brew install emscripten`), then:

```bash
make -C sim web-serve
```

Open http://localhost:8642. The page has to come from a server: opened as a file, the browser
won't load the modules. `make -C sim web` only builds `sim/web-dist/`. The page source is
`sim/web/index.html`, and the C entry points are in `sim/web_main.c`.

## Build it for a board

```bash
DISH=1 tools/flash_square.sh            # the square: build and flash the dish
DISH=1 tools/flash_square.sh --model    # ...and the trained dish model (docs/dish-ai.md)
```

For other boards, apply the board's own overlay first, then `sdkconfig.dish`, in a build
directory of its own:

```bash
cd firmware && idf.py -B build-dish-round -DSDKCONFIG=build-dish-round/sdkconfig "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.round;sdkconfig.dish" build
```

The `dish` workflow (`.github/workflows/dish.yml`) builds all four boards, plus the tank's 1.8
image, on every push to `dish`.

## Not yet checked on a board

- **Frame rate.** On a Mac a busy dish renders in about 0.5 ms per band. The target on the square
  is 20 fps or more with the model thinking; that is an estimate, not a measurement.
  `dish_app.c` logs the fps, every stage's time and the minds' rate every 10 s.
- **Tilt.** The right axis's sign is a knob (`s_right_sign` in `imu_port_qmi8658.c`). If tilting
  right makes the germs drift left, flip it.
- **Touch through `touch_port_read_raw`.** The round board's lift rule (touch_port_poll) is not
  applied to it.
- **The night.** It is a PMIC power-off only: a short PWR press saves and powers off. A board
  without an RTC (the round 1.75C) resumes without living through the time away.
- **Sounds.** They reuse the tank's bank at other pitches.
