/**
 * app_manager.c — tileview + overlay screen manager
 *
 * Tile screens live inside lv_tileview — drag to navigate.
 * Overlay screens slide in over the tileview. Swipe right to dismiss
 * (only works when no sub-modal is open — sub-modals should provide
 * their own explicit close/back buttons).
 */

#include "app_manager.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "app_mgr";

/* ── Tile registry ───────────────────────────────────────────────── */
static lv_obj_t *s_tileview = NULL;
static lv_obj_t *s_tv_screen = NULL;
static const screen_t *s_tiles[APP_MANAGER_MAX_SCREENS];
static lv_obj_t *s_tile_objs[APP_MANAGER_MAX_SCREENS];
static int s_tile_count = 0;
static int s_active_tile = 0;

/* ── Overlay registry ────────────────────────────────────────────── */
static const screen_t *s_overlays[APP_MANAGER_MAX_SCREENS];
static int s_overlay_count = 0;

/* ── Active overlay ──────────────────────────────────────────────── */
static const screen_t *s_cur_overlay = NULL;
static lv_obj_t *s_overlay_screen = NULL;

/* ── Tick timer ──────────────────────────────────────────────────── */
static TimerHandle_t s_tick_timer = NULL;

/* ── Forward declarations ────────────────────────────────────────── */
static void do_close_overlay(void *arg);
static void do_open_overlay(void *arg);

/* ── Tileview change callback ────────────────────────────────────── */
static void tileview_changed_cb(lv_event_t *e)
{
    lv_obj_t *tv = lv_event_get_target(e);
    lv_obj_t *cur = lv_tileview_get_tile_act(tv);
    int new_idx = -1;
    for (int i = 0; i < s_tile_count; i++)
        if (s_tile_objs[i] == cur)
        {
            new_idx = i;
            break;
        }
    if (new_idx < 0 || new_idx == s_active_tile)
        return;
    if (s_tiles[s_active_tile]->on_exit)
        s_tiles[s_active_tile]->on_exit();
    s_active_tile = new_idx;
    if (s_tiles[s_active_tile]->on_enter)
        s_tiles[s_active_tile]->on_enter();
    ESP_LOGI(TAG, "tile → '%s'", s_tiles[s_active_tile]->name);
}

/* ── Overlay back gesture (on overlay screen background only) ────── */
static void overlay_gesture_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev)
        return;
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_RIGHT)
        app_manager_go_back();
}

/* ── Tick ────────────────────────────────────────────────────────── */
static void tick_timer_cb(TimerHandle_t t)
{
    (void)t;
    const screen_t *scr = s_cur_overlay
                              ? s_cur_overlay
                              : (s_tile_count > 0 ? s_tiles[s_active_tile] : NULL);
    if (!scr || !scr->tick)
        return;
    if (lvgl_port_lock(pdMS_TO_TICKS(10)))
    {
        scr->tick();
        lvgl_port_unlock();
    }
}

/* ── Open overlay ────────────────────────────────────────────────── */
typedef struct
{
    const screen_t *screen;
} open_arg_t;
static open_arg_t s_open_arg;

static void do_open_overlay(void *arg)
{
    const screen_t *scr = ((open_arg_t *)arg)->screen;

    if (s_cur_overlay)
    {
        if (s_cur_overlay->on_exit)
            s_cur_overlay->on_exit();
        lv_obj_delete(s_overlay_screen);
        s_overlay_screen = NULL;
        s_cur_overlay = NULL;
    }

    s_overlay_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_overlay_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_overlay_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_overlay_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_overlay_screen, 0, LV_PART_MAIN);

    /* Swipe right on the background dismisses the overlay.
     * Sub-views (pickers, modals) should use explicit back buttons. */
    lv_obj_add_flag(s_overlay_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_overlay_screen, overlay_gesture_cb,
                        LV_EVENT_GESTURE, NULL);

    scr->create(s_overlay_screen);
    s_cur_overlay = scr;

    lv_scr_load_anim(s_overlay_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT,
                     APP_MANAGER_ANIM_MS, 0, false);

    if (scr->on_enter)
        scr->on_enter();
    ESP_LOGI(TAG, "overlay → '%s'", scr->name);
}

/* ── Close overlay ───────────────────────────────────────────────── */
static void do_close_overlay(void *arg)
{
    (void)arg;
    if (!s_cur_overlay)
        return;
    if (s_cur_overlay->on_exit)
        s_cur_overlay->on_exit();

    lv_scr_load_anim(s_tv_screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT,
                     APP_MANAGER_ANIM_MS, 0, false);

    lv_obj_t *old = s_overlay_screen;
    s_overlay_screen = NULL;
    s_cur_overlay = NULL;

    lv_obj_delete_delayed(old, APP_MANAGER_ANIM_MS + 50);
}

/* ── Jump tile ───────────────────────────────────────────────────── */
typedef struct
{
    int idx;
} jump_arg_t;
static jump_arg_t s_jump_arg;

static void do_jump_tile(void *arg)
{
    lv_obj_set_tile_id(s_tileview, ((jump_arg_t *)arg)->idx, 0, LV_ANIM_ON);
}

/* ═══════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════ */

esp_err_t app_manager_init(void)
{
    if (!lvgl_port_lock(pdMS_TO_TICKS(1000)))
        return ESP_FAIL;

    s_tv_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_tv_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_tv_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_tv_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_tv_screen, 0, LV_PART_MAIN);
    lv_scr_load(s_tv_screen);

    s_tileview = lv_tileview_create(s_tv_screen);
    lv_obj_set_size(s_tileview, LCD_HOR_RES, LCD_VER_RES);
    lv_obj_set_pos(s_tileview, 0, 0);
    lv_obj_set_style_bg_color(s_tileview, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_tileview, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_tileview, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_tileview, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_tileview, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(s_tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_tileview, tileview_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    lvgl_port_unlock();

    s_tick_timer = xTimerCreate("mgr_tick",
                                pdMS_TO_TICKS(APP_MANAGER_TICK_MS),
                                pdTRUE, NULL, tick_timer_cb);
    if (!s_tick_timer)
        return ESP_ERR_NO_MEM;
    xTimerStart(s_tick_timer, 0);

    ESP_LOGI(TAG, "tileview ready");
    return ESP_OK;
}

esp_err_t app_manager_register(const screen_t *screen)
{
    if (!screen || !screen->name)
        return ESP_ERR_INVALID_ARG;
    if (s_tile_count >= APP_MANAGER_MAX_SCREENS)
        return ESP_ERR_NO_MEM;
    if (!s_tileview)
        return ESP_ERR_INVALID_STATE;

    if (!lvgl_port_lock(pdMS_TO_TICKS(1000)))
        return ESP_FAIL;

    lv_obj_t *tile = lv_tileview_add_tile(s_tileview, s_tile_count, 0, LV_DIR_HOR);
    lv_obj_set_style_bg_color(tile, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tile, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(tile, 0, LV_PART_MAIN);

    if (screen->create)
        screen->create(tile);

    s_tiles[s_tile_count] = screen;
    s_tile_objs[s_tile_count] = tile;
    s_tile_count++;

    lvgl_port_unlock();

    ESP_LOGI(TAG, "tile '%s' [%d]", screen->name, s_tile_count - 1);
    if (s_tile_count == 1 && screen->on_enter)
        screen->on_enter();
    return ESP_OK;
}

esp_err_t app_manager_register_overlay(const screen_t *screen)
{
    if (!screen || !screen->name)
        return ESP_ERR_INVALID_ARG;
    if (s_overlay_count >= APP_MANAGER_MAX_SCREENS)
        return ESP_ERR_NO_MEM;
    s_overlays[s_overlay_count++] = screen;
    ESP_LOGI(TAG, "overlay '%s'", screen->name);
    return ESP_OK;
}

esp_err_t app_manager_switch_to(const char *name)
{
    for (int i = 0; i < s_overlay_count; i++)
    {
        if (strcmp(s_overlays[i]->name, name) == 0)
        {
            s_open_arg.screen = s_overlays[i];
            lv_async_call(do_open_overlay, &s_open_arg);
            return ESP_OK;
        }
    }
    for (int i = 0; i < s_tile_count; i++)
    {
        if (strcmp(s_tiles[i]->name, name) == 0)
        {
            if (s_cur_overlay)
                lv_async_call(do_close_overlay, NULL);
            s_jump_arg.idx = i;
            lv_async_call(do_jump_tile, &s_jump_arg);
            return ESP_OK;
        }
    }
    ESP_LOGW(TAG, "screen '%s' not found", name);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t app_manager_go_back(void)
{
    if (!s_cur_overlay)
        return ESP_ERR_INVALID_STATE;
    lv_async_call(do_close_overlay, NULL);
    return ESP_OK;
}

const char *app_manager_active_name(void)
{
    if (s_cur_overlay)
        return s_cur_overlay->name;
    return (s_tile_count > 0) ? s_tiles[s_active_tile]->name : NULL;
}