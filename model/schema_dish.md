# Pocket Dish state schema, v1 (tokenizer schema 10)

Every strain in the dish — the pet and every visitor — has a mind. Every few seconds the dish
writes one line about the strain (`dish_encode`, common/dish_brain.c) and asks what to do. The
line has 49 words, all from a closed vocabulary: `dishsim --dish-lexicon` prints it, and
`model/dish_lexicon.txt` is that output, which `train_tokenizer.py --schema 10` reads. The C code
is the only encoder. The sampler (`dishsim --dump-states N seed`) prints the same lines for the
teacher, so the teacher and the board never disagree about the wording.

## The line

```
kind <species> role <pet|friend|threat|rival> count <none|one|few|some|many|swarm>
energy <0-9> health <0-9> comfort <0-9> crowd <0-9>
traits <t> <t> <t>
light <none|near|mid|far> <0-12>
other1 <species> <role> <count> <near|mid|far> <1-12> <goal> <me|them|none>
other2 ... other3 ...
time <day|night> last <goal>
```

- **count:** one is 1, few is up to 5, some up to 20, many up to 60, and swarm is more than that.
- **Drives:** each drive is a strain-wide mean scaled from 0 to 9. Comfort says how well the dish
  (heat, pH and salt) suits the strain. Crowd says how packed its cells are.
- **traits:** the strain's three strongest mutations, written as a word plus a tier. An example
  is `zap2`. Empty slots are `none`. The words are spikes, toxin, zap, jet, engulf, swarm, wall,
  photo, lure, burst, tamer, mimic, scouts, grapple, symbio and quorum. Visitors have no traits.
- **light:** where the child's finger is shining, given as a distance and a clock direction
  (12 is up the glass). With no light the field is `none 0`.
- **other1..3:** the three nearest other strains, nearest first. Each one gives:
  - its kind, role and count
  - its distance and clock direction from this strain
  - what it is doing (its own last goal)
  - who that goal is aimed at: `me`, `them` or `none`
  
  A missing other is written `none none none none 0 none none`.
- **time:** `day` means the lamp is on.

Distances are measured between strain centroids. Near is under 70 px, mid is under 180 px, and
far is anything beyond.

## The answer

```
<goal> <target> urgency <0-9>
```

- **Goals (12):** forage, cluster, explore, rest, divide, spore, seek_light, avoid_light, hunt,
  flee, guard, befriend.
- **Target:** other1, other2, other3 or none. The four goals hunt, flee, guard and befriend need a
  target that is present in the line. Every other goal takes `none`.
- **Urgency:** 0 to 9. The reflexes move a little faster when it is high.

The training document is `<BOS> line -> goal target urgency d`. On the board, each of the three
answer words is chosen from its own closed set by `dish_ai_decide`, and "urgency" is fed in
between. Every answer is therefore a valid decision. The goal's softmax share is how sure the
strain is, and the lens shows it as "SURE 82%".

## Changing it

Change the encoder (`dish_encode`) and the lexicon (`dish_lexicon`) together. Then regenerate
`model/dish_lexicon.txt` and `firmware/main/dish_tokenizer.bin` (docs/dish-ai.md, step 1). Once
traces exist, the line is frozen: a changed line is a new schema, and it needs new traces.
