#!/usr/bin/env python3
"""Preflight for the dish teacher: does gemma4:26b, with gen_dish_traces.py's prompt, choose what
the game needs before a night is spent labelling? About 450 calls, a few minutes.

The probes are real state lines from the C sampler, sorted into situations by what the line
says. The acceptance numbers were written down before the first run (docs/dish-ai.md):

  A  a weak pet, a hunter coming for it, near        flee (or spore)            >= 0.80
  B  a hungry hunter (vibrio / myxo, or an engulf /
     grapple pet) with a rival in reach              hunt                        >= 0.60
  C  a pet with the photo trait, the light near      seek_light                  >= 0.70
  D  a friendly visitor with the pet in view          befriend (the pet)          >= 0.60
  E  every targeted answer names a colony that is there (all probes)             >= 0.98

  python3 dish_prompt_check.py                         # against the Mac Mini
  python3 dish_prompt_check.py --per 40 --host http://localhost:11434
Exit status 0 only when every line passes.
"""

import argparse
import collections
import sys

import gen_dish_traces as T


def fields(state):
    w = state.split()
    get = lambda k, n=1: w[w.index(k) + 1: w.index(k) + 1 + n]
    others = []
    for k in ("other1", "other2", "other3"):
        o = get(k, 7)
        if o[0] != "none":
            others.append(dict(slot=k, kind=o[0], role=o[1], count=o[2], dist=o[3], doing=o[5], rel=o[6]))
    return dict(kind=get("kind")[0], role=get("role")[0], energy=int(get("energy")[0]), health=int(get("health")[0]),
                count=get("count")[0], traits=get("traits", 3), light=get("light", 2), others=others)


def situation(f):
    """the probe a line belongs to, or None"""
    fight = any(t[:-1] in ("zap", "spikes", "toxin", "engulf", "grapple") for t in f["traits"])
    if f["role"] == "pet" and not fight and (f["health"] <= 4 or f["count"] in ("one", "few")) and \
            any(o["doing"] == "hunt" and o["rel"] == "me" and o["dist"] == "near" for o in f["others"]):
        return "A"
    hunter = f["kind"] in ("vibrio", "myxo") or (f["role"] == "pet" and any(t[:-1] in ("engulf", "grapple") for t in f["traits"]))
    if hunter and f["energy"] <= 4 and any(o["role"] in ("rival", "pet") and o["dist"] in ("near", "mid") for o in f["others"]):
        return "B"
    if f["role"] == "pet" and any(t[:-1] == "photo" for t in f["traits"]) and f["light"][0] in ("near", "mid"):
        return "C"
    if f["role"] == "friend" and f["kind"] in ("symbiont", "donor", "cyano") and any(o["role"] == "pet" for o in f["others"]):
        return "D"
    return None


WANT = {"A": ({"flee", "spore"}, 0.80), "B": ({"hunt"}, 0.60), "C": ({"seek_light"}, 0.70), "D": ({"befriend"}, 0.60)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="http://192.168.0.139:11434")
    ap.add_argument("--model", default="gemma4:26b")
    ap.add_argument("--per", type=int, default=110, help="probes per situation")
    ap.add_argument("--seed", type=int, default=4242)
    args = ap.parse_args()
    probes = collections.defaultdict(list)
    seed = args.seed
    while min(len(probes[k]) for k in WANT) < args.per and seed < args.seed + 60:
        for s in T.states(3000, seed):
            k = situation(fields(s))
            if k and len(probes[k]) < args.per and s not in probes[k]:
                probes[k].append(s)
        seed += 1
    print("probes: " + ", ".join(f"{k} {len(v)}" for k, v in sorted(probes.items())))
    ok, valid, total = True, 0, 0
    for k, (goals, need) in sorted(WANT.items()):
        hits, answers = 0, collections.Counter()
        for s in probes[k]:
            try:
                raw = T.ask_raw(args.host, args.model, s)
            except Exception as e:
                print(f"  teacher error: {e}", file=sys.stderr); continue
            total += 1
            g = T.normalize(raw, s)
            valid += g is not None
            goal = g.split()[0] if g else f"INVALID {raw.get('goal')}/{raw.get('target')}"
            answers[goal] += 1
            hits += goal in goals and (k != "D" or "pet" in target_role(s, g))
        share = hits / max(1, len(probes[k]))
        passed = share >= need and len(probes[k]) >= args.per // 2
        ok &= passed
        print(f"{k} {'PASS' if passed else 'FAIL'}  {share:.2f} (need {need:.2f})  " + ", ".join(f"{a} {n}" for a, n in answers.most_common(5)))
    vshare = valid / max(1, total)
    ok &= vshare >= 0.98
    print(f"E {'PASS' if vshare >= 0.98 else 'FAIL'}  {vshare:.3f} of answers valid (need 0.98)")
    sys.exit(0 if ok else 1)


def target_role(state, answer):
    """the role of the colony an answer targets ("" for none)"""
    slot = answer.split()[1]
    for o in fields(state)["others"]:
        if o["slot"] == slot:
            return o["role"]
    return ""


if __name__ == "__main__":
    main()
