/**
 * screen_template.c — copy this for each new screen
 *
 * 1. cp screen_template.c screen_yourname.c
 * 2. Replace "tmpl" with your screen name everywhere
 * 3. In main.c: extern const screen_t screen_yourname;
 *               app_manager_register(&screen_yourname);
 */

#include "screen.h"
#include "app_manager.h"
#include "esp_log.h"

static const char *TAG = "scr_tmpl";

static void tmpl_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, "Template");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(label);
}

static void tmpl_on_enter(void) { ESP_LOGI(TAG, "enter"); }
static void tmpl_on_exit(void) { ESP_LOGI(TAG, "exit"); }

const screen_t screen_template = {
    .name = "template",
    .create = tmpl_create,
    .on_enter = tmpl_on_enter,
    .on_exit = tmpl_on_exit,
    .tick = NULL,
};