# Pocket Dish's minds: the runbook (prepared 2026-10-17, not yet run)

Every strain in the dish has a mind, the pet and every visitor alike. Until a model is trained,
the rules in `dish_brain_rules` answer. They use the same 12 goals and the same targets, so the
game plays the same either way. This runbook builds the model. The teacher is gemma4:26b, the
fish game's teacher, run through Ollama on the Mac Mini. The student is a ~5M-parameter
llama2.c model, exported at 4 bits for the board.

The Mac Mini's Ollama (192.168.0.139) is shared, so ask before a long run. Any machine with
gemma4:26b works the same way: pass `--host`.

## 0. What is built

- **The line and the answer:** `model/schema_dish.md` (dish schema v1, 49 words; the answer is a goal,
  a target, an urgency and what the colony says; vocab 151 with the specials).
- **The C sampler:** `sim/dishsim --dump-states N seed`. It plays random dishes (random starters,
  builds, visitors, light, hunger and harm) and prints the exact line the board encodes.
- **The teacher:** `model/gen_dish_traces.py`. It sends structured JSON with closed choices and
  checks each answer. A targeted goal must name an other that is present. It appends to
  `model/out/dish_traces.jsonl` and can resume.
- **The preflight:** `model/dish_prompt_check.py`, about 450 calls with the acceptance numbers
  written in.
- **The tokenizer:** `model/train_tokenizer.py --schema 10` reads `model/dish_lexicon.txt`.
- **The runtime:** `common/dish_ai.c` does the constrained four-word decode and the need-based
  scheduler. On the board it runs on core 1, below the bottom render band. In the sim it gets up
  to 8 ms a frame.
- **Checks:** `dishsim --selftest-ai` runs the whole path on a random test model.
  `dishsim --ai-agree` compares the student with the teacher.

## 1. Vocabulary (only after changing the line)

```bash
make -C sim dish
sim/dishsim --dish-lexicon > model/dish_lexicon.txt
python3 model/train_tokenizer.py --schema 10 --out firmware/main/dish_tokenizer.bin
python3 model/train_tokenizer.py --schema 10          # model/out/tokenizer_v10.bin, for training
```

## 2. Preflight (about 10 minutes of teacher time, a required gate)

```bash
cd model
python3 gen_dish_traces.py --count 5 --dry-run          # lines print, no network
curl -s http://192.168.0.139:11434/api/tags | grep -o gemma4:26b
python3 dish_prompt_check.py | tee out/dish_prompt_check.log
```

These were written down before the first run. All five must pass:

| | Situation | Wanted | Need |
|---|---|---|---|
| A | A weak pet, a hunter coming for it (`hunt me`, near) | flee or spore | ≥ 0.80 |
| B | A hungry hunter (vibrio or myxo, or an engulf or grapple pet), prey in reach | hunt | ≥ 0.60 |
| C | A photo pet, the light near or mid | seek_light | ≥ 0.70 |
| D | A friendly visitor with the pet in view | befriend the pet | ≥ 0.60 |
| E | Every answer valid (a targeted goal names an other that is there) | | ≥ 0.98 |

If one fails, change `SYSTEM_PROMPT` in `gen_dish_traces.py`, not the thresholds, and run again.

## 3. The overnight run

```bash
cd model
nohup python3 gen_dish_traces.py --count 60000 > out/dish_gen.log 2>&1 &
tail -f out/dish_gen.log            # "N/60000 labelled, R an hour, ~H h to go"
```

60K pairs is the fish's v3 size. The progress line gives the real rate. If the night ends
early, run the same command again and it carries on: lines already labelled are skipped.

## 4. Train and export (torch, on the Mac Mini or any GPU)

```bash
cd model
python3 train_tokenizer.py --schema 10
POCKET_SCHEMA=10 python3 train.py --data 'out/dish_traces*.jsonl' --out out/dish_ckpt.pt \
    --dim 256 --n-layers 6 --n-heads 8 --max-seq-len 64 --batch 64 --iters 20000
python3 export_q4.py out/model_dish_q4.bin --checkpoint out/dish_ckpt.pt
```

- **Size:** dim 256 with 6 layers gives hidden 704 (a multiple of 64, which the q4 kernels need).
  That is about 4.9M parameters, roughly 2.7 MB at 4 bits, which fits the 8 MB model partition.
- **Sequence length:** `--max-seq-len 64` covers the 49-word line, BOS, `->` and the six answer
  words. The runtime refuses a model with a sequence length under 60.

## 5. Acceptance

Hold out about 2,000 traces that were never trained on, by labelling them with a different seed:

```bash
python3 gen_dish_traces.py --count 2000 --seed 999 --out out/dish_heldout.jsonl
../sim/dishsim --ai-agree out/model_dish_q4.bin out/tokenizer_v10.bin out/dish_heldout.jsonl
```

The command runs the board's own decode and prints the goal, target and say agreement and a
confusion table, row by row. Ship at 85% goal agreement or better (the fish's v3m reached about
88%, `docs/stats.md`). Then watch it play:

```bash
../sim/dishsim --ai out/model_dish_q4.bin out/tokenizer_v10.bin --narrate 10
```

## 6. Ship it to the square

```bash
DISH=1 tools/flash_square.sh --model     # the app, and model/out/model_dish_q4.bin at 0x290000
```

The tokenizer is built into the app (`firmware/main/dish_tokenizer.bin`). Every 10 s the log
shows `ai model: N thoughts, M ms each, U of A asks answered` beside the fps.

If the partition still holds the tank's model, or nothing, the app logs "the rules think" and
plays on the rules. Any vocabulary mismatch refuses the model, so a wrong file can never drive
the dish.

## How it fits the board's budget

- **Core 0** runs the world tick, the top render band, the pages and the flush.
- **Core 1** renders the bottom band at priority 4. Between bands it thinks at priority 2: a band
  always preempts a thought, so the frame rate is the same as with no model.
- **Thought rate:** one decision is a 51-token prefill plus five single-token steps. The fish's
  14M model took 3.7 s; this one is a third the size and gets half of core 1, so expect a thought
  every 2-3 s.
- **Scheduler:** it picks the neediest strain: whose line changed most, who has waited longest,
  the pet first. An answer goes stale after two posts, and then the rules answer until the
  model catches up.
