/* dish_app.c — Pocket Dish on the board (CONFIG_POCKET_DISH, the dish branch).
 *
 * Built INSTEAD of main.c (main/CMakeLists.txt): the same ports, the same four
 * boards, a different game. Kept deliberately small next to main.c - the tank's
 * night machinery (light sleep, the drowse, the clockless deep sleep), the
 * director console and update mode are not here yet:
 *   - the PWR key: a short press saves and powers the board off (the PMIC);
 *     the next press is a cold boot, and the dish lives the time away
 *     through in one step (dish_sleep) when the board has a clock
 *   - the frame is drawn in two bands, one per core: core 1 has no model to
 *     run in this game, so it renders the bottom half while core 0 does the top
 *   - the save is one NVS blob (dish_save), written every minute and at power-off
 * ponytail: power-off is the only night; light sleep and the tank's drowse
 * come back when the dish has been lived with on a real board. */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_app_desc.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "psram_plan.h"
#include "display_port.h"
#include "touch_port.h"
#include "battery_port.h"
#include "imu_port.h"
#include "audio_port.h"
#include "codec_port.h"
#include "brightness.h"
#include "rtc_port.h"
#include "progression.h"          /* clock_port_now_unix (persist_port_esp.c) */
#include "dish.h"
#include "dish_render.h"
#include "dish_ui.h"

#ifdef CONFIG_POCKET_TANK_DISPLAY_SH8601
extern i2c_master_bus_handle_t board_i2c_bus(void);
#else
static i2c_master_bus_handle_t board_i2c_bus(void) { return NULL; }
#endif

static const char *TAG = "pocket-dish";
#define FRAME_MIN_MS 40                         /* 25 fps, the tank's ceiling */
#define SAVE_EVERY_US (60LL * 1000000)

static dish_t *dish;
static dish_ui_t ui;
static uint16_t *fb[2];
static bool s_pmic, s_nvs;

/* render.c and update.c (linked through the ports) ask for this */
const char *version_port_string(void) { return esp_app_get_description()->version; }

/* ---- the save: one blob, and when it was written ---- */
static void save_dish(void) {
    if (!s_nvs) return;
    static uint8_t buf[DISH_SAVE_MAX];
    size_t n = dish_save(dish, buf, sizeof buf);
    nvs_handle_t h;
    if (!n || nvs_open("dish", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "sav", buf, n);
    nvs_set_i64(h, "at", clock_port_now_unix());
    nvs_commit(h);
    nvs_close(h);
}
/* true: a dish came back (and lived through the time since, if the clock knows it) */
static bool load_dish(void) {
    if (!s_nvs) return false;
    static uint8_t buf[DISH_SAVE_MAX];
    size_t n = sizeof buf; int64_t at = 0;
    nvs_handle_t h;
    if (nvs_open("dish", NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = nvs_get_blob(h, "sav", buf, &n) == ESP_OK;
    nvs_get_i64(h, "at", &at);
    nvs_close(h);
    if (!ok || !dish_load(dish, buf, n, (uint32_t)esp_timer_get_time() | 1)) return false;
    int64_t now = clock_port_now_unix();
    if (at > 0 && now > at) {
        dish_sleep(dish, (float)(now - at));
        ESP_LOGI(TAG, "the dish lived %.1f h away", (now - at) / 3600.0f);
    } else ESP_LOGI(TAG, "no clock across the power-off: the dish resumes where it was");
    return true;
}

static void on_event(int ev, int strain, void *ud) { (void)ud; dish_ui_event(&ui, dish, ev, strain); }

/* ---- core 1: the bottom half of each frame ---- */
static SemaphoreHandle_t s_go, s_done;
static uint16_t *s_band_fb;
static int64_t s_band_us;
static void band_task(void *arg) {
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_go, portMAX_DELAY);
        int64_t t0 = esp_timer_get_time();
        dish_render_band(dish, s_band_fb, DISH_W, DISH_H / 2, DISH_H);
        s_band_us += esp_timer_get_time() - t0;
        xSemaphoreGive(s_done);
    }
}

static void power_off(void) {
    ESP_LOGI(TAG, "PWR: saving, powering off (a press brings the dish back)");
    save_dish();
    audio_port_sleep();
    display_port_sleep();
    vTaskDelay(pdMS_TO_TICKS(50));
    battery_port_poweroff();
    display_port_wake();                        /* no PMIC after all: carry on */
}

/* ---- core 0: the game ---- */
static void dish_task(void *arg) {
    (void)arg;
    int64_t last = esp_timer_get_time(), last_log = last, last_save = last;
    int64_t tick_us = 0, top_us = 0, wait_us = 0, ui_us = 0, flush_us = 0; int frames = 0, cur = 0;
    bool was_down = false; float lx = 0, ly = 0;
    for (;;) {
        int64_t now = esp_timer_get_time();
        float dt = (now - last) / 1e6f; last = now; if (dt > 0.25f) dt = 0.25f;
        if (s_pmic && battery_port_key_poll() == 1) power_off();
        imu_port_poll(now);
#ifndef TANK_WATCH                              /* worn on a wrist, the picture never follows the arm */
        bool inv = imu_port_inverted();
        display_port_set_inverted(inv);
        touch_port_set_inverted(inv);
#endif
        /* the finger: a raw read each frame, the release at the last place it was */
        float x, y;
        bool down = touch_port_read_raw(&x, &y);
        if (down) { lx = x; ly = y; dish_ui_touch(&ui, dish, x, y, true); }
        else if (was_down) dish_ui_touch(&ui, dish, lx, ly, false);
        was_down = down;
        dish_tick(dish, dt, dish_brain_rules);
        dish_ui_tick(&ui, dish, dt);
        for (int pitch, cue; (cue = dish_ui_take_sound(&ui, &pitch)) >= 0;) audio_port_play(cue, pitch);
        brightness_apply(!dish->light);
        int64_t t0 = esp_timer_get_time(); tick_us += t0 - now;
        /* the frame: core 1 the bottom half, this core the top, then the pages over both */
        uint16_t *f = fb[cur];
        if (f) {
            s_band_fb = f;
            xSemaphoreGive(s_go);
            dish_render_band(dish, f, DISH_W, 0, DISH_H / 2);
            int64_t t1 = esp_timer_get_time(); top_us += t1 - t0;
            xSemaphoreTake(s_done, portMAX_DELAY);
            int64_t t2 = esp_timer_get_time(); wait_us += t2 - t1;
            dish_ui_render(&ui, dish, f, DISH_W);
            int64_t t3 = esp_timer_get_time(); ui_us += t3 - t2;
            display_port_flush(f);
            flush_us += esp_timer_get_time() - t3;
            frames++;
            if (fb[1]) cur ^= 1;
        }
        if (now - last_save > SAVE_EVERY_US) { save_dish(); last_save = now; }
        if (now - last_log > 10 * 1000000LL && frames) {
            const strain_t *pet = &dish->strain[DISH_PET];
            ESP_LOGI(TAG, "%.1f fps | tick %.1f ms, top band %.1f, bottom band %.1f (wait %.1f), ui %.1f, flush %.1f | %d cells, %s %d (%s), DNA %u | heap int %u KB",
                     frames * 1e6f / (now - last_log), tick_us / 1e3f / frames, top_us / 1e3f / frames, s_band_us / 1e3f / frames,
                     wait_us / 1e3f / frames, ui_us / 1e3f / frames, flush_us / 1e3f / frames,
                     dish->n_live, pet->name, pet->count, STRAT_NAMES[pet->strat.id], (unsigned)dish->dna,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
            tick_us = top_us = wait_us = ui_us = flush_us = s_band_us = 0; frames = 0; last_log = now;
        }
        int rest = (int)((FRAME_MIN_MS * 1000 - (esp_timer_get_time() - now)) / 1000);
        vTaskDelay(pdMS_TO_TICKS(rest < 1 ? 1 : rest));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "pocket dish (build %s), a %d x %d dish", version_port_string(), DISH_W, DISH_H);
    gpio_deep_sleep_hold_dis();
    s_nvs = nvs_flash_init() == ESP_OK;
    if (!s_nvs) ESP_LOGE(TAG, "NVS would not start: the dish runs without saving (nothing erased)");
    brightness_init();
    for (int i = 0; i < 2; i++) fb[i] = heap_caps_aligned_alloc(64, PLAN_FB_ALLOC, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint16_t *scene = heap_caps_aligned_alloc(64, PLAN_FB_ALLOC, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint16_t *bg = heap_caps_aligned_alloc(64, PLAN_FB_ALLOC, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    dish_render_set_scene(scene);
    dish_render_set_background(bg);
    /* the board, as main.c brings it up: the display owns the I2C bus, then the glass, the PMIC and its rails */
    display_port_init();
    touch_port_init();
    s_pmic = battery_port_init(board_i2c_bus());
    if (!board_has_expander()) {                /* the round board: A3V3 feeds the ES7210 whole (battery_port.h) */
        battery_port_pin_rail("aldo1");
        codec_port_mic_adc_down(board_i2c_bus());
    }
    if (board_is_watch()) battery_port_pin_rail("aldo2");   /* the watch's panel power */
    battery_port_trim_rails();
    battery_port_key_init();
    rtc_port_init(board_i2c_bus());             /* the wall clock: how long the dish was away */
    codec_port_init(board_i2c_bus());
    audio_port_init(board_i2c_bus());
    imu_port_init(board_i2c_bus());
    /* the world: internal RAM if it fits with room to spare (no model in this game), else PSRAM */
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > sizeof(dish_t) + 96 * 1024)
        dish = heap_caps_calloc(1, sizeof(dish_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!dish) dish = heap_caps_calloc(1, sizeof(dish_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!dish) {                                /* no room for the world at all: say so, rather than crash in a loop */
        ESP_LOGE(TAG, "no memory for the dish (%u B): internal %u KB, PSRAM %u KB free - stopping", (unsigned)sizeof(dish_t),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
        return;
    }
    dish_ui_init(&ui);
    dish_events_set(on_event, NULL);
    if (!load_dish()) {
        dish_init(dish, (uint32_t)esp_timer_get_time() | 1, SP_BACILLUS, "BLIP");
        dish_ui_begin_setup(&ui);
        ESP_LOGI(TAG, "a new dish: the first-run setup");
    } else ESP_LOGI(TAG, "%s is back: %d strains, DNA %u", dish->strain[DISH_PET].name, DISH_MAX_STRAINS, (unsigned)dish->dna);
    if (fb[0]) dish_render(dish, fb[0], DISH_W);   /* one whole frame first: the shader's tables and the caches, built by one core */
    s_go = xSemaphoreCreateBinary(); s_done = xSemaphoreCreateBinary();
    ESP_LOGI(TAG, "dish %u B in %s, internal heap %u KB free", (unsigned)sizeof(dish_t),
             esp_ptr_external_ram(dish) ? "PSRAM" : "internal RAM",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    xTaskCreatePinnedToCore(band_task, "band", 6144, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(dish_task, "dish", 12288, NULL, 4, NULL, 0);
}
