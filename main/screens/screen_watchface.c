#include "screen.h"
#include "clock_service.h"
#include "esp_log.h"
#include <time.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "scr_watchface";

static lv_obj_t *s_time_label = NULL;
static lv_obj_t *s_date_label = NULL;
static lv_obj_t *s_sync_dot = NULL;

static void watchface_create(lv_obj_t *parent)
{
    ESP_LOGI(TAG, "create");
    lv_obj_set_style_bg_color(parent, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

#if LV_FONT_MONTSERRAT_48
    const lv_font_t *time_font = &lv_font_montserrat_48;
#else
    const lv_font_t *time_font = &lv_font_montserrat_24;
#endif

    s_time_label = lv_label_create(parent);
    lv_label_set_text(s_time_label, "00:00");
    lv_obj_set_style_text_font(s_time_label, time_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_time_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_time_label, -1, LV_PART_MAIN);
    lv_obj_set_width(s_time_label, LCD_HOR_RES);
    lv_obj_set_style_text_align(s_time_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_time_label, LV_ALIGN_CENTER, 0, -16);

    s_date_label = lv_label_create(parent);
    lv_label_set_text(s_date_label, "Monday  01 Jan");
    lv_obj_set_style_text_font(s_date_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_date_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_opa(s_date_label, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_date_label, 3, LV_PART_MAIN);
    lv_obj_set_width(s_date_label, LCD_HOR_RES);
    lv_obj_set_style_text_align(s_date_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_date_label, LV_ALIGN_CENTER, 0, 36);

    s_sync_dot = lv_obj_create(parent);
    lv_obj_set_size(s_sync_dot, 8, 8);
    lv_obj_set_style_radius(s_sync_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_sync_dot, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_sync_dot, lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_sync_dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(s_sync_dot, LV_ALIGN_TOP_RIGHT, -20, 20);
}

static void watchface_tick(void)
{
    if (!s_time_label || !s_date_label)
        return;

    time_t now = clock_service_now();
    struct tm t;
    localtime_r(&now, &t);

    char time_buf[8];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d", t.tm_hour, t.tm_min);
    lv_label_set_text(s_time_label, time_buf);

    char day[12], mon_day[12], date_buf[32];
    strftime(day, sizeof(day), "%A", &t);
    strftime(mon_day, sizeof(mon_day), "%d %b", &t);
    snprintf(date_buf, sizeof(date_buf), "%s  %s", day, mon_day);
    lv_label_set_text(s_date_label, date_buf);

    if (s_sync_dot)
    {
        lv_color_t dot_col = clock_service_is_synced()
                                 ? lv_color_make(0x22, 0xDD, 0x22)
                                 : lv_color_make(0x44, 0x44, 0x44);
        lv_obj_set_style_bg_color(s_sync_dot, dot_col, LV_PART_MAIN);
    }
}

static void watchface_on_enter(void)
{
    const char *tz = getenv("TZ");
    if (tz)
    {
        setenv("TZ", tz, 1);
        tzset();
    }
}

static void watchface_on_exit(void)
{
    /* Labels belong to the tile lv_obj which persists — don't null them.
     * Only null if the screen is actually being destroyed (overlay). */
}

const screen_t screen_watchface = {
    .name = "watchface",
    .create = watchface_create,
    .on_enter = watchface_on_enter,
    .on_exit = watchface_on_exit,
    .tick = watchface_tick,
};