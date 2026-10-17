# pocket-tank PC simulator (LVGL v9 + SDL2)
#
# macOS: built as x86_64 by default because the Homebrew SDL2 here is the
# Intel build (/usr/local); the sim runs fine under Rosetta. `make ARCH=` for
# a native build against an arm64 SDL2. Elsewhere (Linux, CI) ARCH is empty
# and the host compiler's own architecture is used.

CC      := cc
ifeq ($(shell uname -s),Darwin)
ARCH    ?= -arch x86_64
endif
ARCH    ?=
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)

# `make ROUND=1`: the round 466 px bowl (the Waveshare 1.75C's world) as
# ./fishsim-round, built beside the rectangle's in build-round/.
# `make WATCH=1`: the watch's 410 x 502 portrait tank (the Waveshare 2.06) as ./fishsim-watch
# `make SQUARE=1`: the square's 480 x 480 tank (the Waveshare 2.16) as ./fishsim-square
ifdef ROUND
BUILD   := build-round
OUT     := fishsim-round
ROUNDF  := -DTANK_ROUND
else ifdef WATCH
BUILD   := build-watch
OUT     := fishsim-watch
ROUNDF  := -DTANK_WATCH
else ifdef SQUARE
BUILD   := build-square
OUT     := fishsim-square
ROUNDF  := -DTANK_SQUARE
else
BUILD   := build
OUT     := fishsim
ROUNDF  :=
endif

CFLAGS  := $(ROUNDF) -O2 -std=c11 $(ARCH) -I. -I../common -I../common/llm -DLV_CONF_INCLUDE_SIMPLE -DSOUNDS_BIN='"../assets/sounds/sounds.bin"' -DPT_VERSION='"$(shell git describe --always --dirty --exclude='*' 2>/dev/null || echo sim)"' $(SDL_CFLAGS) \
           -Wall -Wextra -Wno-unused-parameter -MMD -MP
LDFLAGS := $(ARCH) $(SDL_LIBS) -lm

APP_SRCS  := main.c advisor_llm.c persist_port_sim.c net_port_sim.c ../common/tank.c ../common/update.c ../common/advisor.c ../common/render.c ../common/icons.c ../common/progression.c ../common/setup.c ../common/audio.c ../common/notice.c ../common/sounds.c ../common/battery.c ../common/llm/q4_model.c ../common/llm/word_tok.c ../common/llm/advisor_core.c

LVGL_SRCS := $(shell find lvgl/src -name '*.c' 2>/dev/null)
OBJS      := $(patsubst %.c,$(BUILD)/%.o,$(filter-out ../common/%,$(APP_SRCS)) $(LVGL_SRCS)) $(patsubst ../common/%.c,$(BUILD)/common/%.o,$(filter ../common/%,$(APP_SRCS)))

$(OUT): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

SELFTESTS := --selftest --selftest-hunger --selftest-shop --selftest-battery \
             --selftest-pop --selftest-card --selftest-fry --selftest-sleep --selftest-tend --selftest-llm \
             --selftest-saves --selftest-update
# the dev repo does not track model/out (the public one does): NOMODEL=1 runs
# the same checks without the LLM selftest, for a checkout with no model
ifdef NOMODEL
SELFTESTS := $(filter-out --selftest-llm,$(SELFTESTS))
endif

# `make check` tests the rectangle; `make ROUND=1 check` / `make WATCH=1 check` /
# `make SQUARE=1 check` the same selftests in that board's world; `make check-all`
# all four - the gate for anything in common/ (docs/BOARDS.md)
check: $(OUT)
	@set -e; for test in $(SELFTESTS); do echo "RUN $(OUT) $$test"; ./$(OUT) "$$test"; done

check-all:
	$(MAKE) check
	$(MAKE) ROUND=1 check
	$(MAKE) WATCH=1 check
	$(MAKE) SQUARE=1 check

check-nomodel:
	$(MAKE) NOMODEL=1 check

# ---- Pocket Dish (the dish branch, 2026-10-16) ----
# `make dish` -> ./dishsim: headless always (selftests, --snapshot); a window too
# when sdl2-config is found. ROUND=1 / WATCH=1 / SQUARE=1 pick the glass as above.
# Native arch (no $(ARCH)): it needs no LVGL and links a native SDL2 if there is one.
DISH_SRCS := dish_main.c ../common/dish.c ../common/dish_brain.c ../common/dish_render.c ../common/dish_ui.c
DISH_OUT  := $(subst fishsim,dishsim,$(OUT))
ifneq ($(shell command -v sdl2-config 2>/dev/null),)
DISH_SDL  := -DDISH_SDL $(SDL_CFLAGS)
DISH_LIBS := $(SDL_LIBS)
endif
dish: $(DISH_OUT)
$(DISH_OUT): $(DISH_SRCS) $(wildcard ../common/dish*.h)
	$(CC) $(ROUNDF) -O2 -std=c11 -I../common -Wall -Wextra -Wno-unused-parameter $(DISH_SDL) -o $@ $(DISH_SRCS) $(DISH_LIBS) -lm
dish-check: $(DISH_OUT)
	./$(DISH_OUT) --selftest-dish && ./$(DISH_OUT) --selftest-pet
dish-check-all:
	$(MAKE) dish-check
	$(MAKE) ROUND=1 dish-check
	$(MAKE) WATCH=1 dish-check
	$(MAKE) SQUARE=1 dish-check
.PHONY: dish dish-check dish-check-all

$(BUILD)/common/llm/%.o: ../common/llm/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -O3 -c $< -o $@

$(BUILD)/common/%.o: ../common/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build build-round build-watch build-square fishsim fishsim-round fishsim-watch fishsim-square

-include $(OBJS:.o=.d)

.PHONY: check check-all check-nomodel clean
