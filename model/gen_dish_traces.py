#!/usr/bin/env python3
"""The teacher for Pocket Dish: gemma4:26b labels what every strain's mind should do.

The states come from the C sim itself (dishsim --dump-states), so the line the
teacher reads is byte for byte the line the board encodes (schema_dish.md):
there is no Python copy of the encoder to drift. For each line the teacher
answers a JSON object with closed choices (Ollama structured output), and a
{"state": ..., "goal": "<goal> <target> urgency <d>"} line is appended to the
JSONL file train.py reads.

  python3 gen_dish_traces.py --count 20 --dry-run      # print states, no network
  python3 gen_dish_traces.py --count 60000             # the overnight run (resumable: it appends)

Stdlib only. Transport is curl, as in gen_traces.py (see its note on why).
"""

import argparse
import json
import os
import random
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
DISHSIM = os.path.join(HERE, "..", "sim", "dishsim")
GOALS = ["forage", "cluster", "explore", "rest", "divide", "spore", "seek_light", "avoid_light",
         "hunt", "flee", "guard", "befriend"]
TARGETED = {"hunt", "flee", "guard", "befriend"}
TARGETS = ["other1", "other2", "other3", "none"]

SYSTEM_PROMPT = """You are the mind of one colony of microbes in a petri dish. A child keeps the dish:
they shine light on it (food grows where light falls), stir currents, tilt it, and pick mutations
for their own colony (the PET). Other microbes VISIT: some are friends, some threats, some rivals.
Every colony - the pet and every visitor - is asked what to do, and you answer for the one
described.

The line describes the colony: its kind and role (pet, friend, threat, rival), how many cells
(none, one, few, some, many, swarm), its drives 0-9 (energy = fed, health, comfort = how well the
dish suits it, crowd = how packed it is), its traits (mutations with a tier 1-3; none = empty),
the child's light (distance near/mid/far and a clock direction, 12 = up; none 0 = no light),
up to three other colonies (other1..other3, nearest first: kind, role, count, distance, clock
direction, what that colony is doing, and whether it is doing it to me, to them, or none),
the time (day: the lamp is on) and its own last goal.

Answer with a goal, a target and an urgency 0-9:
- forage: look for food. divide: split while fed. rest: save energy (night, or nothing to do).
- cluster: huddle together (safety in numbers, a wall). explore: spread out to new ground.
- spore: go dormant to survive (badly hurt, or the dish is harsh) - only kinds that can.
- seek_light: come to the child's light (green / photosynthesis, or food blooms there).
- avoid_light: shy away from it.
- hunt <other>: attack that colony. flee <other>: get away from it.
- guard <other>: put yourself between that colony and what threatens it.
- befriend <other>: go to it and stay close (friendly kinds share food).
The four last goals need a target (other1, other2 or other3, one that exists); every other goal
takes target none.

What each kind wants:
- vibrio, myxo: hunters. They pick off the WEAKEST colony in reach when hungry, rest when full.
- phage: a virus. It hunts any cells but its own; it never rests.
- ciliate: a guardian. It hunts phages and hunters, and guards the pet otherwise.
- symbiont, donor, cyano: friends. They befriend the pet and stay close.
- coccus, lacto, halo: rivals. They forage, divide and crowd the others out; they flee hunters.
- mold: it grows slowly and rests.
- The pet's kind (bacillus, coccus, spirillum, cyano, vibrio or myxo) plays its traits:
  zap, spikes, toxin, engulf and grapple colonies can fight back (hunt the threat, if healthy);
  jet colonies flee fast; swarm and wall colonies cluster; photo colonies seek light;
  lure colonies wait for prey to come; spore-burst and bacillus/myxo colonies spore when beaten.
  A weak pet with a hunter coming for it (hunt me, near) flees. A hungry pet forages or hunts.
Urgency: 9 = life or death now, 5 = ordinary, 1 = idle."""


def output_schema():
    return {
        "type": "object",
        "properties": {
            "goal": {"type": "string", "enum": GOALS},
            "target": {"type": "string", "enum": TARGETS},
            "urgency": {"type": "integer", "minimum": 0, "maximum": 9},
        },
        "required": ["goal", "target", "urgency"],
    }


def others_present(state):
    """which of other1..3 the line actually has (an absent one reads 'otherN none ...')"""
    w = state.split()
    return {w[i] for i in range(len(w) - 1) if w[i] in ("other1", "other2", "other3") and w[i + 1] != "none"}


def normalize(obj, state):
    """the teacher's answer as the student's three words, or None if it is not a valid decision"""
    goal, target, urg = obj.get("goal"), obj.get("target"), obj.get("urgency")
    if goal not in GOALS or target not in TARGETS:
        return None
    urg = urg if isinstance(urg, int) and 0 <= urg <= 9 else 5
    if goal in TARGETED:
        if target == "none" or target not in others_present(state):
            return None                           # a hunt at no one is no decision
    else:
        target = "none"
    return f"{goal} {target} urgency {urg}"


def ask_raw(host, model, state, timeout=40.0, temperature=0.7):
    """the teacher's JSON answer, as it came"""
    body = json.dumps({
        "model": model,
        "messages": [{"role": "system", "content": SYSTEM_PROMPT}, {"role": "user", "content": state}],
        "format": output_schema(), "stream": False, "think": False, "keep_alive": "10m",
        "options": {"temperature": temperature, "num_predict": 48, "num_ctx": 1024},
    }).encode()
    proc = subprocess.run(["curl", "-s", "--max-time", str(int(timeout)), f"{host}/api/chat",
                           "-H", "Content-Type: application/json", "-d", "@-"],
                          input=body, capture_output=True, timeout=timeout + 10)
    if proc.returncode != 0 or not proc.stdout:
        raise OSError(f"curl failed rc={proc.returncode}")
    return json.loads(json.loads(proc.stdout)["message"]["content"])


def ask(host, model, state, timeout):
    return normalize(ask_raw(host, model, state, timeout), state)


def states(n, seed):
    """n state lines from the C sampler"""
    out = subprocess.run([DISHSIM, "--dump-states", str(n), str(seed)], capture_output=True, text=True, check=True).stdout
    return [l for l in out.splitlines() if l.startswith("kind ")]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--count", type=int, required=True)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--host", default="http://192.168.0.139:11434", help="Ollama base URL (the Mac Mini)")
    ap.add_argument("--model", default="gemma4:26b")
    ap.add_argument("--out", default=os.path.join(HERE, "out", "dish_traces.jsonl"))
    ap.add_argument("--seed", type=int, default=None, help="the sampler's seed (default: random, so resumed runs see new dishes)")
    ap.add_argument("--timeout", type=float, default=40.0)
    args = ap.parse_args()
    if not os.path.exists(DISHSIM):
        sys.exit("no sim/dishsim: make -C sim dish")
    seed = args.seed if args.seed is not None else random.randrange(1, 1 << 30)
    if args.dry_run:
        for s in states(args.count, seed):
            print(s)
        return
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    seen = set()
    if os.path.exists(args.out):
        with open(args.out) as f:
            seen = {json.loads(l)["state"] for l in f if l.strip()}
    done, bad, t0 = 0, 0, time.time()
    with open(args.out, "a") as f:
        while done < args.count:
            batch = [s for s in states(min(500, args.count - done) + 50, seed) if s not in seen]
            seed += 1
            for s in batch:
                if done >= args.count:
                    break
                try:
                    goal = ask(args.host, args.model, s, args.timeout)
                except (OSError, ValueError, KeyError, subprocess.TimeoutExpired) as e:
                    print(f"  teacher error: {e}", file=sys.stderr); time.sleep(2); continue
                if goal is None:
                    bad += 1; continue
                f.write(json.dumps({"state": s, "goal": goal}) + "\n"); f.flush()
                seen.add(s); done += 1
                if done % 50 == 0:
                    rate = done / (time.time() - t0) * 3600
                    print(f"{done}/{args.count} labelled ({bad} refused), {rate:.0f} an hour, ~{(args.count - done) / max(rate, 1):.1f} h to go")
    print(f"done: {done} lines in {args.out} ({bad} answers refused as invalid)")


if __name__ == "__main__":
    main()
