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
DISH_SRCS := dish_main.c ../common/dish.c ../common/dish_brain.c ../common/dish_render.c ../common/dish_ui.c ../common/dish_prog.c ../common/dish_mut.c ../common/dish_story.c ../common/dish_talk.c ../common/dish_quest.c ../common/dish_ai.c ../common/llm/q4_model.c ../common/llm/word_tok.c
DISH_OUT  := $(subst fishsim,dishsim,$(OUT))
ifneq ($(shell command -v sdl2-config 2>/dev/null),)
DISH_SDL  := -DDISH_SDL $(SDL_CFLAGS)
DISH_LIBS := $(SDL_LIBS)
endif
dish: $(DISH_OUT)
$(DISH_OUT): $(DISH_SRCS) $(wildcard ../common/dish*.h)
	$(CC) $(ROUNDF) -O2 -std=c11 -I../common -Wall -Wextra -Wno-unused-parameter $(DISH_SDL) -o $@ $(DISH_SRCS) $(DISH_LIBS) -lm
dish-check: $(DISH_OUT)
	./$(DISH_OUT) --selftest-dish && ./$(DISH_OUT) --selftest-pet && ./$(DISH_OUT) --selftest-evolve && ./$(DISH_OUT) --selftest-prog && ./$(DISH_OUT) --selftest-render && ./$(DISH_OUT) --selftest-run && ./$(DISH_OUT) --selftest-visitors && ./$(DISH_OUT) --selftest-verbs && ./$(DISH_OUT) --selftest-mind && ./$(DISH_OUT) --selftest-ai && ./$(DISH_OUT) --selftest-play
dish-check-all:
	$(MAKE) dish-check
	$(MAKE) ROUND=1 dish-check
	$(MAKE) WATCH=1 dish-check
	$(MAKE) SQUARE=1 dish-check
.PHONY: dish dish-check dish-check-all

# ---- Pocket Dish in a browser (Emscripten): `make web` -> web-dist/, `make web-serve` opens it on localhost ----
# One WebAssembly module per glass (the shape is compiled in, as on the boards), the page, and the sound bank.
WEB_SRCS := web_main.c ../common/dish.c ../common/dish_brain.c ../common/dish_render.c ../common/dish_ui.c ../common/dish_prog.c ../common/dish_mut.c ../common/dish_story.c ../common/dish_talk.c ../common/dish_quest.c ../common/sounds.c
WEB_FLAGS := -O2 -std=c11 -I../common -sMODULARIZE=1 -sEXPORT_NAME=createDish -sENVIRONMENT=web -sFILESYSTEM=0 \
             -sINITIAL_MEMORY=16MB -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU16,HEAP32 \
             -sEXPORTED_FUNCTIONS=_web_w,_web_h,_web_fb,_web_save_buf,_web_init,_web_frame,_web_touch,_web_save,_web_sound,_web_key,_web_fast,_web_reset,_web_tilt
web: $(WEB_SRCS) $(wildcard ../common/dish*.h) web/index.html
	@mkdir -p web-dist
	emcc $(WEB_FLAGS) -o web-dist/dish-rect.js $(WEB_SRCS)
	emcc $(WEB_FLAGS) -DTANK_ROUND -o web-dist/dish-round.js $(WEB_SRCS)
	emcc $(WEB_FLAGS) -DTANK_WATCH -o web-dist/dish-watch.js $(WEB_SRCS)
	emcc $(WEB_FLAGS) -DTANK_SQUARE -o web-dist/dish-square.js $(WEB_SRCS)
	@# the bank as a WAV (16 kHz mono s16, as it is): a type browsers decode and artifact hosts serve
	python3 -c "import wave,sys; w=wave.open('web-dist/sounds.wav','wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000); w.writeframes(open('../assets/sounds/sounds.bin','rb').read()); w.close()"
	@# the page is written for the artifact host, which adds the document around it; locally it gets that here
	@{ printf '<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'; cat web/index.html; printf '\n</html>\n'; } > web-dist/index.html
	@echo "built web-dist/ - run: make web-serve, then open http://localhost:8642"
web-serve: web
	cd web-dist && python3 -m http.server 8642
.PHONY: web web-serve

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
