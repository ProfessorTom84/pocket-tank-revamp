/* dish_talk.c — see dish_talk.h. */
#include "dish_talk.h"
#include <stdio.h>
#include <string.h>

const char *const SAY_WORDS[SAY_COUNT] = { "hello", "pal", "share", "thanks", "protect", "lunch", "taunt", "attack", "run",
    "scared", "hide", "hungry", "yum", "grow", "sleepy", "curious", "sunny", "shade", "squished", "together", "brave", "ouch",
    "proud", "bored", "infect", "zap", "bye", "help" };
const uint8_t SAY_EMOJI[SAY_COUNT] = { EMO_WAVE, EMO_HEART, EMO_FOOD, EMO_HAPPY, EMO_SHIELD, EMO_YUM, EMO_ANGRY, EMO_BOLT, EMO_SCARED,
    EMO_SCARED, EMO_MOON, EMO_FOOD, EMO_YUM, EMO_SPARKLE, EMO_ZZZ, EMO_EYES, EMO_SUN, EMO_MOON, EMO_ANGRY, EMO_HEART, EMO_SHIELD,
    EMO_SAD, EMO_STAR, EMO_QUESTION, EMO_YUM, EMO_BOLT, EMO_WAVE, EMO_EXCLAIM };
static const char *const LINES[SAY_COUNT][3] = {
    [SAY_HELLO]    = { "HI!", "HELLO THERE, {T}!", "OH! NEW FACES IN THE DISH. WELCOME!" },
    [SAY_FRIEND]   = { "FRIENDS?", "I LIKE YOU, {T}.", "LET'S STICK TOGETHER, OKAY? YOU AND ME." },
    [SAY_SHARE]    = { "HAVE SOME!", "I MADE FOOD FOR YOU.", "TAKE IT, {T}. THERE'S PLENTY TO GO AROUND." },
    [SAY_THANKS]   = { "THANKS!", "YOU'RE THE BEST.", "THANK YOU! I FEEL STRONGER ALREADY." },
    [SAY_PROTECT]  = { "BEHIND ME!", "I'VE GOT YOU, {T}.", "NOBODY TOUCHES MY FRIENDS WHILE I'M HERE." },
    [SAY_LUNCH]    = { "LUNCH!", "YOU LOOK DELICIOUS.", "COME HERE, LITTLE SNACK. I WON'T BITE... MUCH." },
    [SAY_TAUNT]    = { "TOO SLOW!", "YOU CAN'T HIDE, {T}.", "THIS DISH IS MINE NOW. EVERY LAST CRUMB." },
    [SAY_ATTACK]   = { "CHARGE!", "GET THEM, EVERYONE!", "FOR THE COLONY! NOBODY PUSHES US AROUND!" },
    [SAY_RUN]      = { "RUN!", "IT'S {T}! RUN!", "EVERYBODY SCATTER, THEY'RE COMING FOR US!" },
    [SAY_SCARED]   = { "EEK!", "I DON'T LIKE THIS.", "SOMETHING BIG IS WATCHING US..." },
    [SAY_HIDE]     = { "HIDE!", "CURL UP, EVERYONE.", "TIME TO TURN TO SPORES AND WAIT IT OUT." },
    [SAY_HUNGRY]   = { "HUNGRY...", "IS THERE ANY FOOD?", "MY TUMMY IS RUMBLING. SHINE SOME LIGHT?" },
    [SAY_YUM]      = { "YUM!", "THAT HIT THE SPOT.", "MMM, SUGARS. MY FAVORITE KIND OF FOOD." },
    [SAY_GROW]     = { "SPLIT TIME!", "WE'RE GROWING!", "ONE BECOMES TWO, TWO BECOME FOUR..." },
    [SAY_SLEEPY]   = { "ZZZ...", "SO SLEEPY.", "WAKE ME UP WHEN THE LIGHTS COME ON." },
    [SAY_CURIOUS]  = { "OOH?", "WHAT'S OVER THERE?", "LET'S GO SEE WHAT'S AT THE EDGE OF THE DISH." },
    [SAY_SUNNY]    = { "LIGHT!", "WARM AND BRIGHT!", "THE GIANT IS SHINING A LIGHT. FOLLOW IT!" },
    [SAY_SHADE]    = { "TOO BRIGHT!", "TURN IT DOWN!", "THAT LIGHT STINGS. LET'S FIND SOME SHADE." },
    [SAY_SQUISHED] = { "SQUISHED!", "MOVE OVER!", "IT'S GETTING CROWDED IN HERE, SCOOT!" },
    [SAY_TOGETHER] = { "HUDDLE!", "STAY CLOSE.", "TOGETHER WE'RE A WALL NOTHING CAN BREAK." },
    [SAY_BRAVE]    = { "COME ON THEN!", "WE'RE NOT AFRAID!", "YOU PICKED THE WRONG COLONY, {T}." },
    [SAY_OUCH]     = { "OUCH!", "THAT HURT!", "WE LOST SOME OF US... STAY STRONG." },
    [SAY_PROUD]    = { "LOOK AT US!", "WE'RE THE BEST!", "WE'RE THE MIGHTIEST GERMS IN THE WHOLE DISH!" },
    [SAY_BORED]    = { "BORED...", "ANYONE THERE?", "PSST, GIANT! POKE ME, I WANT TO PLAY." },
    [SAY_INFECT]   = { "LET ME IN!", "KNOCK KNOCK.", "YOUR INSIDES LOOK COMFY. I'M MOVING IN!" },
    [SAY_ZAP]      = { "ZAP!", "BZZT! BACK OFF!", "ONE MORE STEP AND YOU GET THE SPARKS." },
    [SAY_BYE]      = { "BYE!", "SEE YOU AROUND.", "THIS DISH IS TOO WILD FOR ME. GOODBYE!" },
    [SAY_HELP]     = { "HELP!", "GIANT, HELP US!", "SOMEBODY DO SOMETHING, {T} IS EATING US!" },
};

int dish_say_text(int say, int variant, const char *target, char *out, int n) {
    if (say < 0 || say >= SAY_COUNT) say = SAY_CURIOUS;
    if (variant < 0 || variant > 2) variant = 0;
    const char *src = LINES[say][variant];
    if (strstr(src, "{T}") && (!target || !*target)) src = LINES[say][0];   /* (no one to name: the short one) */
    int k = 0;
    for (const char *p = src; *p && k < n - 1; p++) {
        if (p[0] == '{' && p[1] == 'T' && p[2] == '}') {
            for (const char *t = target; *t && k < n - 1; t++) out[k++] = (char)(*t >= 'a' && *t <= 'z' ? *t - 32 : *t);
            p += 2;
        } else out[k++] = *p;
    }
    out[k] = 0;
    return SAY_EMOJI[say];
}
int dish_say_variant(const dish_t *d, int s) {
    uint32_t h = (uint32_t)(d->run_t / 20) * 2654435761u + (uint32_t)s * 40503u;
    h ^= h >> 15;
    return (int)(h % 3);
}

static bool threat_near(const dish_t *d, int s) {
    for (int q = 0; q < 3; q++) {
        int o = d->strain[s].other[q];
        if (o >= 0 && d->strain[o].used && d->strain[o].count && d->strain[o].strat.id == ST_HUNT && d->strain[o].strat.target == s) return true;
    }
    return false;
}
uint8_t dish_say_rules(const dish_t *d, int s, int goal, int target) {
    const strain_t *st = &d->strain[s];
    int sp = st->species;
    bool pet = s == DISH_PET, chased = threat_near(d, s);
    if (sp == SP_PHAGE) return SAY_INFECT;
    if (pet && chased && st->health < 0.45f) return SAY_HELP;
    switch (goal) {
    case ST_HUNT:
        if (pet) return chased ? SAY_BRAVE : SAY_ATTACK;
        if (sp == SP_CILIATE) return SAY_PROTECT;
        return st->energy < 0.5f ? SAY_LUNCH : SAY_TAUNT;
    case ST_FLEE: return st->health < 0.5f ? SAY_SCARED : SAY_RUN;
    case ST_GUARD: return SAY_PROTECT;
    case ST_BEFRIEND: return sp == SP_SYMBIONT || sp == SP_DONOR ? SAY_SHARE : (target == DISH_PET && !pet ? SAY_FRIEND : SAY_HELLO);
    case ST_FORAGE: return st->energy < 0.4f ? SAY_HUNGRY : st->crowd > 0.7f ? SAY_SQUISHED : SAY_CURIOUS;
    case ST_DIVIDE: return SAY_GROW;
    case ST_REST: return d->light ? SAY_BORED : SAY_SLEEPY;
    case ST_CLUSTER: return chased ? SAY_SCARED : SAY_TOGETHER;
    case ST_EXPLORE: return st->crowd > 0.6f ? SAY_SQUISHED : SAY_CURIOUS;
    case ST_SEEK_LIGHT: return SAY_SUNNY;
    case ST_AVOID_LIGHT: return SAY_SHADE;
    case ST_SPORE: return SAY_HIDE;
    }
    return pet && st->count > 40 ? SAY_PROUD : SAY_CURIOUS;
}

int dish_poke_text(const dish_t *d, int s, bool flicked, char *out, int n) {
    static const char *const POKE[] = { "HEY!", "THAT TICKLES!", "BOOP!", "WHO'S THERE?", "HI, GIANT!", "PLEASE DON'T SQUISH ME.",
                                        "OH! A FINGER THE SIZE OF A MOUNTAIN!", "I WAS NAPPING..." };
    static const char *const FLICK[] = { "WHEEE!", "AAAAH!", "PUT ME DOWN!", "I CAN FLY!", "NOT AGAIN!" };
    uint32_t h = (uint32_t)(d->clock * 7) * 2654435761u + (uint32_t)s;
    h ^= h >> 13;
    if (flicked) { snprintf(out, (size_t)n, "%s", FLICK[h % 5]); return h & 1 ? EMO_STAR : EMO_SCARED; }
    snprintf(out, (size_t)n, "%s", POKE[h % 8]);
    return (h >> 3) & 1 ? EMO_HAPPY : EMO_EYES;
}
