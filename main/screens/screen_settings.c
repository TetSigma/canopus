/**
 * screen_settings.c — Registry-driven settings screen (uses ui_kit)
 */

#include "screen.h"
#include "app_manager.h"
#include "settings_registry.h"
#include "clock_service.h"
#include "ui_kit.h"
#include "ui_theme.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <time.h>

static const char *TAG = "scr_settings";

#define AXP2101_ADDR 0x34
#define AXP2101_BAT_PERCENT 0xA4
#define NVS_NS_CLOCK "clock_svc"
#define NVS_KEY_TZ "tz_name"

extern i2c_master_bus_handle_t s_i2c_bus;

static lv_obj_t *s_root = NULL;
static lv_obj_t *s_bat_label = NULL;
static volatile bool s_alive = false;

/* ── Dismiss helper ──────────────────────────────────────────────── */
static void dismiss_cb(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_user_data(e);
    if (obj)
        lv_obj_delete(obj);
}

/* ═══════════════════════════════════════════════════════════════════
 * Battery
 * ═══════════════════════════════════════════════════════════════════ */
static int read_battery_percent(void)
{
    i2c_master_dev_handle_t dev = NULL;
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDR,
        .scl_speed_hz = 400000,
    };
    if (i2c_master_bus_add_device(s_i2c_bus, &cfg, &dev) != ESP_OK)
        return -1;
    uint8_t reg = AXP2101_BAT_PERCENT, val = 0;
    i2c_master_transmit(dev, &reg, 1, pdMS_TO_TICKS(50));
    i2c_master_receive(dev, &val, 1, pdMS_TO_TICKS(50));
    i2c_master_bus_rm_device(dev);
    return (int)(val & 0x7F);
}

static void bat_task(void *arg)
{
    int pct = read_battery_percent();
    if (s_alive && s_bat_label && lvgl_port_lock(pdMS_TO_TICKS(200)))
    {
        char buf[12];
        snprintf(buf, sizeof(buf), pct < 0 ? "N/A" : "%d%%", pct);
        lv_label_set_text(s_bat_label, buf);
        lvgl_port_unlock();
    }
    vTaskDelete(NULL);
}

/* ═══════════════════════════════════════════════════════════════════
 * Timezone picker
 * ═══════════════════════════════════════════════════════════════════ */
typedef struct
{
    const char *name;
    const char *tz;
} tz_entry_t;
static const tz_entry_t TZ_LIST[] = {
    {"UTC", "UTC0"},
    {"Warsaw (CET)", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"London (GMT)", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"New York (EST)", "EST5EDT,M3.2.0,M11.1.0"},
    {"Chicago (CST)", "CST6CDT,M3.2.0,M11.1.0"},
    {"Denver (MST)", "MST7MDT,M3.2.0,M11.1.0"},
    {"LA (PST)", "PST8PDT,M3.2.0,M11.1.0"},
    {"Moscow (MSK)", "MSK-3"},
    {"Dubai (GST)", "GST-4"},
    {"India (IST)", "IST-5:30"},
    {"China (CST)", "CST-8"},
    {"Japan (JST)", "JST-9"},
    {"Sydney (AEST)", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
};
#define TZ_COUNT ((int)(sizeof(TZ_LIST) / sizeof(TZ_LIST[0])))

typedef struct
{
    int idx;
    lv_obj_t *overlay;
} tz_sel_ctx_t;
static tz_sel_ctx_t s_tz_ctxs[TZ_COUNT];

static void tz_selected_cb(lv_event_t *e)
{
    tz_sel_ctx_t *ctx = (tz_sel_ctx_t *)lv_event_get_user_data(e);
    if (!ctx || ctx->idx < 0 || ctx->idx >= TZ_COUNT)
        return;

    /* Apply immediately — don't defer to tick */
    setenv("TZ", TZ_LIST[ctx->idx].tz, 1);
    tzset();

    /* Verify it took effect */
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    ESP_LOGI(TAG, "TZ set to '%s' → local time now %02d:%02d",
             TZ_LIST[ctx->idx].tz, t.tm_hour, t.tm_min);

    /* Save to NVS */
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CLOCK, NVS_READWRITE, &h) == ESP_OK)
    {
        nvs_set_str(h, NVS_KEY_TZ, TZ_LIST[ctx->idx].tz);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "timezone → %s (%s)", TZ_LIST[ctx->idx].name, TZ_LIST[ctx->idx].tz);

    /* Signal screens with s_last_min cache to reset */
    clock_service_force_tick_refresh();

    if (ctx->overlay)
        lv_obj_delete(ctx->overlay);
}

static void show_tz_picker(lv_obj_t *parent)
{
    lv_obj_t *overlay = uk_overlay(parent);

    uk_screen_title(overlay, "Timezone");

    /* List — 300px wide, centred, leaves room for cancel button */
    lv_obj_t *list = lv_list_create(overlay);
    lv_obj_set_size(list, 300, LCD_VER_RES - 130);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_color(list, UK_COL_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 2, LV_PART_MAIN);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_CHAIN_VER);

    for (int i = 0; i < TZ_COUNT; i++)
    {
        lv_obj_t *btn = lv_list_add_button(list, NULL, TZ_LIST[i].name);
        lv_obj_set_style_bg_color(btn, lv_color_make(0x1A, 0x1A, 0x1A), LV_PART_MAIN);
        lv_obj_set_style_text_color(btn, UK_COL_WHITE, LV_PART_MAIN);
        lv_obj_set_style_text_font(btn, UK_FONT, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
        s_tz_ctxs[i].idx = i;
        s_tz_ctxs[i].overlay = overlay;
        lv_obj_add_event_cb(btn, tz_selected_cb, LV_EVENT_CLICKED, &s_tz_ctxs[i]);
    }

    /* Cancel — pinned to bottom, always visible */
    lv_obj_t *cancel = uk_button_ghost(overlay, "Cancel");
    lv_obj_set_width(cancel, 160);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_add_event_cb(cancel, dismiss_cb, LV_EVENT_CLICKED, overlay);
}

/* ═══════════════════════════════════════════════════════════════════
 * WiFi restart
 * ═══════════════════════════════════════════════════════════════════ */
static void restart_wifi_task(void *arg)
{
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_wifi_connect();
    vTaskDelete(NULL);
}
static void on_tap_restart_wifi(lv_obj_t *parent)
{
    (void)parent;
    xTaskCreate(restart_wifi_task, "wifi_rst", 4096, NULL, 2, NULL);
}

/* ═══════════════════════════════════════════════════════════════════
 * Factory reset modal
 * ═══════════════════════════════════════════════════════════════════ */
static void do_factory_reset(lv_event_t *e)
{
    (void)e;
    nvs_flash_erase();
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

static void on_tap_factory_reset(lv_obj_t *parent)
{
    /* Semi-transparent overlay */
    lv_obj_t *overlay = uk_overlay(parent);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_80, LV_PART_MAIN);

    /* Card */
    lv_obj_t *card = uk_modal_card(overlay);

    lv_obj_t *title = uk_label(card, "Factory Reset?");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *body = uk_label_muted(card, "Erases all settings.\nCannot be undone.");
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(body, UK_MODAL_W - 40);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 54);

    lv_obj_t *cancel = uk_button_ghost(card, "Cancel");
    lv_obj_set_size(cancel, 130, 42);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 10, -12);
    lv_obj_add_event_cb(cancel, dismiss_cb, LV_EVENT_CLICKED, overlay);

    lv_obj_t *reset = uk_button_danger(card, "Reset");
    lv_obj_set_size(reset, 130, 42);
    lv_obj_align(reset, LV_ALIGN_BOTTOM_RIGHT, -10, -12);
    lv_obj_add_event_cb(reset, do_factory_reset, LV_EVENT_CLICKED, NULL);
}

/* ═══════════════════════════════════════════════════════════════════
 * Built-in settings groups
 * ═══════════════════════════════════════════════════════════════════ */
static lv_color_t s_red = {.red = 0xFF, .green = 0x44, .blue = 0x44};

static const settings_row_t s_system_rows[] = {
    {LV_SYMBOL_LOOP, "Timezone", "Tap >", NULL, show_tz_picker},
    {LV_SYMBOL_WIFI, "Restart WiFi", "Tap >", NULL, on_tap_restart_wifi},
    {LV_SYMBOL_TRASH, "Factory Reset", "Tap >", &s_red, on_tap_factory_reset},
};

/* ── External groups — add one per screen ────────────────────────── */
/* extern const settings_group_t screen_heartrate_settings; */

static const settings_group_t *GROUPS[] = {
    /* &screen_heartrate_settings, */
};
#define GROUPS_COUNT ((int)(sizeof(GROUPS) / sizeof(GROUPS[0])))

/* ═══════════════════════════════════════════════════════════════════
 * Row renderer
 * ═══════════════════════════════════════════════════════════════════ */
typedef struct
{
    lv_obj_t *parent;
    void (*on_tap)(lv_obj_t *);
} tap_ctx_t;
static tap_ctx_t s_tap_ctxs[32];
static int s_tap_ctx_count = 0;

static void row_tap_cb(lv_event_t *e)
{
    tap_ctx_t *ctx = (tap_ctx_t *)lv_event_get_user_data(e);
    if (ctx && ctx->on_tap && ctx->parent)
        ctx->on_tap(ctx->parent);
}

static void render_group(lv_obj_t *list, const char *header,
                         const settings_row_t *rows, int count)
{
    if (header)
    {
        lv_obj_t *h = uk_label_section(list, header);
        lv_obj_set_width(h, UK_ROW_W);
    }

    for (int i = 0; i < count; i++)
    {
        const settings_row_t *r = &rows[i];

        lv_obj_t *row = uk_row_titled(list, r->icon, r->title);

        if (r->value)
        {
            lv_color_t vc = r->value_color ? *r->value_color : UK_COL_MUTED;
            uk_row_set_value(row, r->value, vc);
        }

        if (r->on_tap && s_tap_ctx_count < 32)
        {
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
            tap_ctx_t *ctx = &s_tap_ctxs[s_tap_ctx_count++];
            ctx->parent = s_root;
            ctx->on_tap = r->on_tap;
            lv_obj_add_event_cb(row, row_tap_cb, LV_EVENT_CLICKED, ctx);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * create()
 * ═══════════════════════════════════════════════════════════════════ */
static void settings_create(lv_obj_t *parent)
{
    ESP_LOGI(TAG, "create");
    s_root = parent;
    s_bat_label = NULL;
    s_alive = true;
    s_tap_ctx_count = 0;

    uk_screen_bg(parent);
    uk_screen_title(parent, LV_SYMBOL_SETTINGS "  Settings");

    lv_obj_t *list = uk_list(parent);
    lv_obj_set_size(list, UK_ROW_W, LCD_VER_RES - UK_LIST_Y - 30);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, UK_LIST_Y + 30);

    /* Battery row — special because value updates live */
    lv_obj_t *bat_row = uk_row_titled(list, LV_SYMBOL_BATTERY_FULL, "Battery");
    s_bat_label = uk_row_set_value(bat_row, "...", UK_COL_MUTED);
    xTaskCreate(bat_task, "bat_read", 2048, NULL, 1, NULL);

    /* Built-in system rows */
    render_group(list, NULL, s_system_rows,
                 sizeof(s_system_rows) / sizeof(s_system_rows[0]));

    /* External screen groups */
    for (int g = 0; g < GROUPS_COUNT; g++)
        render_group(list, GROUPS[g]->title, GROUPS[g]->rows, GROUPS[g]->count);
}

static void settings_on_exit(void)
{
    s_alive = false;
    s_root = NULL;
    s_bat_label = NULL;
    s_tap_ctx_count = 0;
}

const screen_t screen_settings = {
    .name = "settings",
    .create = settings_create,
    .on_enter = NULL,
    .on_exit = settings_on_exit,
    .tick = NULL,
};