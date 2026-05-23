/**
 * screen_wifi.c — WiFi setup screen (uses ui_kit)
 */

#include "screen.h"
#include "app_manager.h"
#include "ui_kit.h"
#include "ui_theme.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "scr_wifi";

#define NVS_NAMESPACE "wifi_cfg"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "pass"
#define MAX_NETWORKS 12
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define LIST_W 320 /* narrower than screen — fits circle */

/* ── State ───────────────────────────────────────────────────────── */
static lv_obj_t *s_root = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_loading_label = NULL;
static bool s_loading_anim = false;
static uint8_t s_loading_frame = 0;
static wifi_ap_record_t s_ap_list[MAX_NETWORKS];
static uint16_t s_ap_count = 0;
static char s_selected_ssid[33] = {0};
static EventGroupHandle_t s_wifi_eg = NULL;
static volatile bool s_alive = false;

static const char *const LOAD_FRAMES[] = {"|", "/", "-", "\\"};

static bool wifi_ui_active(void) { return s_alive && s_root != NULL; }

/* ── Loading spinner ─────────────────────────────────────────────── */
static void wifi_hide_loading(void)
{
    s_loading_anim = false;
    if (s_loading_label && lv_obj_is_valid(s_loading_label))
        lv_obj_delete(s_loading_label);
    s_loading_label = NULL;
}

static void wifi_show_loading(lv_obj_t *parent, int y_ofs)
{
    wifi_hide_loading();
    s_loading_label = lv_label_create(parent);
    lv_label_set_text(s_loading_label, LOAD_FRAMES[0]);
    lv_obj_set_style_text_font(s_loading_label, UK_FONT_LARGE, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_loading_label, UK_COL_WHITE, LV_PART_MAIN);
    lv_obj_align(s_loading_label, LV_ALIGN_CENTER, 0, y_ofs);
    s_loading_frame = 0;
    s_loading_anim = true;
}

static void wifi_tick(void)
{
    if (!s_loading_anim || !s_loading_label ||
        !lv_obj_is_valid(s_loading_label))
        return;
    s_loading_frame = (s_loading_frame + 1) & 3;
    lv_label_set_text(s_loading_label, LOAD_FRAMES[s_loading_frame]);
}

/* ── NVS ─────────────────────────────────────────────────────────── */
static void nvs_save_wifi(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
        return;
    nvs_set_str(h, NVS_KEY_SSID, ssid);
    nvs_set_str(h, NVS_KEY_PASS, pass);
    nvs_commit(h);
    nvs_close(h);
}

static bool nvs_load_ssid(char *ssid, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
        return false;
    bool ok = (nvs_get_str(h, NVS_KEY_SSID, ssid, &len) == ESP_OK) && strlen(ssid) > 0;
    nvs_close(h);
    return ok;
}

/* ── WiFi events ─────────────────────────────────────────────────── */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (!s_wifi_eg)
        return;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
        xEventGroupSetBits(s_wifi_eg, WIFI_FAIL_BIT);
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
        xEventGroupSetBits(s_wifi_eg, WIFI_CONNECTED_BIT);
}

static void show_network_list(lv_obj_t *parent);
static void show_password_view(lv_obj_t *parent, const char *ssid);

/* ═══════════════════════════════════════════════════════════════════
 * Connect task
 * ═══════════════════════════════════════════════════════════════════ */
static char s_task_ssid[33];
static char s_task_pass[64];

static void wifi_connect_task(void *arg)
{
    if (s_wifi_eg)
        vEventGroupDelete(s_wifi_eg);
    s_wifi_eg = xEventGroupCreate();

    esp_event_handler_instance_t h1, h2;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        wifi_event_handler, NULL, &h1);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        wifi_event_handler, NULL, &h2);

    wifi_config_t wcfg = {0};
    memcpy(wcfg.sta.ssid, s_task_ssid, sizeof(wcfg.sta.ssid) - 1);
    memcpy(wcfg.sta.password, s_task_pass, sizeof(wcfg.sta.password) - 1);
    wcfg.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    wcfg.sta.pmf_cfg.capable = false;
    wcfg.sta.pmf_cfg.required = false;
    esp_wifi_set_config(WIFI_IF_STA, &wcfg);
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(s_wifi_eg,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdTRUE, pdFALSE, pdMS_TO_TICKS(20000));

    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, h1);
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, h2);

    const char *result = (bits & WIFI_CONNECTED_BIT) ? "Connected!"
                         : (bits & WIFI_FAIL_BIT)    ? "Failed — wrong password?"
                                                     : "Timed out";

    if (s_alive && lvgl_port_lock(pdMS_TO_TICKS(300)))
    {
        if (s_status_label)
            lv_label_set_text(s_status_label, result);
        wifi_hide_loading();
        lvgl_port_unlock();
    }
    vTaskDelete(NULL);
}

/* ═══════════════════════════════════════════════════════════════════
 * View 3: Connecting
 * ═══════════════════════════════════════════════════════════════════ */
static void show_status_view(lv_obj_t *parent, const char *ssid,
                             const char *pass)
{
    if (!wifi_ui_active())
        return;
    wifi_hide_loading();
    lv_obj_clean(parent);
    s_status_label = NULL;

    lv_obj_t *t = uk_label(parent, "Connecting...");
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *sl = lv_label_create(parent);
    lv_label_set_text(sl, ssid);
    lv_obj_set_style_text_font(sl, UK_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(sl, lv_color_make(0x88, 0xCC, 0xFF), LV_PART_MAIN);
    lv_obj_align(sl, LV_ALIGN_CENTER, 0, -10);

    s_status_label = uk_label_muted(parent, "Please wait...");
    lv_obj_align(s_status_label, LV_ALIGN_CENTER, 0, 30);

    wifi_show_loading(parent, 50);

    nvs_save_wifi(ssid, pass);
    memcpy(s_task_ssid, ssid, sizeof(s_task_ssid) - 1);
    s_task_ssid[32] = 0;
    memcpy(s_task_pass, pass, sizeof(s_task_pass) - 1);
    s_task_pass[63] = 0;
    xTaskCreate(wifi_connect_task, "wifi_conn", 8192, NULL, 3, NULL);
}

/* ═══════════════════════════════════════════════════════════════════
 * View 2: Password entry
 * ═══════════════════════════════════════════════════════════════════ */
static void kb_ready_cb(lv_event_t *e)
{
    lv_obj_t *kb = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_t *ta = lv_keyboard_get_textarea(kb);
    if (!ta || !s_root)
        return;

    static char pass_copy[64], ssid_copy[33];
    const char *raw = lv_textarea_get_text(ta);
    memset(pass_copy, 0, sizeof(pass_copy));
    if (raw)
    {
        size_t l = strlen(raw);
        if (l >= 64)
            l = 63;
        memcpy(pass_copy, raw, l);
    }
    memset(ssid_copy, 0, sizeof(ssid_copy));
    memcpy(ssid_copy, s_selected_ssid, 32);

    ESP_LOGI(TAG, "connect ssid='%s' pass_len=%d", ssid_copy, (int)strlen(pass_copy));
    show_status_view(s_root, ssid_copy, pass_copy);
}

static void cancel_pw_cb(lv_event_t *e)
{
    (void)e;
    if (wifi_ui_active())
        show_network_list(s_root);
}

static void show_password_view(lv_obj_t *parent, const char *ssid)
{
    if (!wifi_ui_active())
        return;
    wifi_hide_loading();
    lv_obj_clean(parent);
    s_status_label = NULL;

    char buf[48];
    snprintf(buf, sizeof(buf), "Password for:\n%s", ssid);
    lv_obj_t *title = uk_label(parent, buf);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(title, LIST_W);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 64);

    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, LIST_W, 36);
    lv_obj_align_to(ta, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_textarea_set_password_mode(ta, false);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 63);
    lv_textarea_set_placeholder_text(ta, "Password");
    lv_obj_set_style_text_font(ta, UK_FONT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ta, lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, UK_COL_WHITE, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN);

    lv_obj_t *cancel = uk_button_ghost(parent, "Cancel");
    lv_obj_set_size(cancel, LIST_W, 36);
    lv_obj_align_to(cancel, ta, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    lv_obj_add_event_cb(cancel, cancel_pw_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *kb = lv_keyboard_create(parent);
    lv_obj_set_size(kb, LIST_W, 165);
    lv_keyboard_set_textarea(kb, ta);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_set_style_pad_row(kb, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_column(kb, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(kb, 2, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_make(0x1A, 0x1A, 0x1A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(kb, lv_color_make(0x33, 0x33, 0x33), LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, UK_COL_WHITE, LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb, lv_color_make(0x55, 0x55, 0x55), LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, kb_ready_cb, LV_EVENT_READY, kb);
    lv_obj_align_to(kb, cancel, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
}

/* ═══════════════════════════════════════════════════════════════════
 * View 1: Network list
 * ═══════════════════════════════════════════════════════════════════ */
static void exit_wifi_cb(lv_event_t *e)
{
    (void)e;
    app_manager_go_back();
}

static void network_tap_cb(lv_event_t *e)
{
    const char *ssid = (const char *)lv_event_get_user_data(e);
    if (!ssid || !s_root)
        return;
    memcpy(s_selected_ssid, ssid, 32);
    s_selected_ssid[32] = 0;
    show_password_view(s_root, ssid);
}

static void show_network_list(lv_obj_t *parent)
{
    if (!wifi_ui_active())
        return;
    wifi_hide_loading();
    lv_obj_clean(parent);
    s_status_label = NULL;

    uk_screen_title(parent, "WiFi");

    lv_obj_t *exit_btn = uk_button_ghost(parent, "Exit");
    lv_obj_set_size(exit_btn, 60, 28);
    lv_obj_align(exit_btn, LV_ALIGN_TOP_LEFT, 8, 10);
    lv_obj_add_event_cb(exit_btn, exit_wifi_cb, LV_EVENT_CLICKED, NULL);

    /* Scrollable block, centred */
    lv_obj_t *block = lv_obj_create(parent);
    lv_obj_set_size(block, LIST_W, LV_SIZE_CONTENT);
    lv_obj_align(block, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_opa(block, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(block, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(block, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(block, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(block, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);

    char saved_ssid[33] = {0};
    bool has_saved = nvs_load_ssid(saved_ssid, sizeof(saved_ssid));

    if (has_saved)
    {
        lv_obj_t *sh = uk_label_section(block, "SAVED");
        lv_obj_set_width(sh, LIST_W);
        lv_obj_set_style_text_align(sh, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

        lv_obj_t *row = lv_obj_create(block);
        lv_obj_set_size(row, LIST_W, 44);
        lv_obj_set_style_bg_color(row, lv_color_make(0x08, 0x22, 0x08), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(row, UK_ROW_RADIUS, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 8, LV_PART_MAIN);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        static char s_saved_cb[33];
        memcpy(s_saved_cb, saved_ssid, sizeof(s_saved_cb));
        lv_obj_add_event_cb(row, network_tap_cb, LV_EVENT_CLICKED, s_saved_cb);

        char row_txt[40];
        snprintf(row_txt, sizeof(row_txt), "%.24s  Saved", saved_ssid);
        lv_obj_t *slbl = uk_label(row, row_txt);
        lv_obj_set_width(slbl, LIST_W - 16);
        lv_obj_set_style_text_align(slbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(slbl, LV_ALIGN_CENTER, 0, 0);
    }

    lv_obj_t *ah = uk_label(block, "Select Network");
    lv_obj_set_width(ah, LIST_W);
    lv_obj_set_style_text_align(ah, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    if (s_ap_count == 0)
    {
        lv_obj_t *none = uk_label_muted(block, "No networks found");
        lv_obj_set_width(none, LIST_W);
        lv_obj_set_style_text_align(none, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        return;
    }

    lv_obj_t *list = lv_obj_create(block);
    lv_obj_set_size(list, LIST_W, 200);
    lv_obj_set_style_bg_color(list, UK_COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    for (uint16_t i = 0; i < s_ap_count; i++)
    {
        if (has_saved && strcmp((char *)s_ap_list[i].ssid, saved_ssid) == 0)
            continue;

        char label[52];
        snprintf(label, sizeof(label), "%.28s  (%d dBm)",
                 (char *)s_ap_list[i].ssid, (int)s_ap_list[i].rssi);

        lv_obj_t *row = lv_obj_create(list);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, 38);
        lv_obj_set_style_bg_color(row, lv_color_make(0x1A, 0x1A, 0x1A), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(row, UK_ROW_RADIUS, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 8, LV_PART_MAIN);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, network_tap_cb, LV_EVENT_CLICKED,
                            (void *)s_ap_list[i].ssid);

        lv_obj_t *lbl = uk_label(row, label);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
    }
}

/* ── Scan task ───────────────────────────────────────────────────── */
static void scan_task(void *arg)
{
    (void)arg;
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));
    wifi_scan_config_t scan_cfg = {.show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE};
    esp_wifi_scan_start(&scan_cfg, true);
    s_ap_count = MAX_NETWORKS;
    esp_wifi_scan_get_ap_records(&s_ap_count, s_ap_list);
    ESP_LOGI(TAG, "Found %d networks", s_ap_count);
    if (s_alive && lvgl_port_lock(pdMS_TO_TICKS(500)))
    {
        if (s_root)
            show_network_list(s_root);
        lvgl_port_unlock();
    }
    vTaskDelete(NULL);
}

/* ═══════════════════════════════════════════════════════════════════
 * create / on_exit
 * ═══════════════════════════════════════════════════════════════════ */
static void wifi_create(lv_obj_t *parent)
{
    ESP_LOGI(TAG, "create");
    s_root = parent;
    s_status_label = NULL;
    s_alive = true;

    uk_screen_bg(parent);
    lv_obj_t *t = uk_label(parent, "Scanning WiFi...");
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -30);
    wifi_show_loading(parent, 40);
    xTaskCreate(scan_task, "wifi_scan", 8192, NULL, 2, NULL);
}

static void wifi_on_exit(void)
{
    s_alive = false;
    wifi_hide_loading();
    s_root = NULL;
    s_status_label = NULL;
}

const screen_t screen_wifi = {
    .name = "wifi",
    .create = wifi_create,
    .on_enter = NULL,
    .on_exit = wifi_on_exit,
    .tick = wifi_tick,
};