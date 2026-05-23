#pragma once

/**
 * settings_registry.h — Declarative settings rows
 *
 * Each screen that wants to contribute settings defines a
 * settings_group_t and exports it as e.g. screen_wifi_settings.
 *
 * screen_settings.c collects all registered groups and renders them.
 *
 * Usage in a screen file:
 *
 *   static void on_tap_tz(lv_obj_t *parent) { show_tz_picker(parent); }
 *
 *   static const settings_row_t wifi_rows[] = {
 *       { LV_SYMBOL_WIFI, "Restart WiFi", "Tap >", NULL,  restart_wifi_cb },
 *   };
 *   const settings_group_t screen_wifi_settings = {
 *       .title = "WiFi",
 *       .rows  = wifi_rows,
 *       .count = 1,
 *   };
 *
 * Then in screen_settings.c's registry array add &screen_wifi_settings.
 */

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* A single row in the settings list */
    typedef struct
    {
        const char *icon;        /* LV_SYMBOL_* prefix, e.g. LV_SYMBOL_WIFI  */
        const char *title;       /* Row left label                            */
        const char *value;       /* Row right label (static hint text)        */
        lv_color_t *value_color; /* NULL = default grey; pointer to override  */

        /* Called when the row is tapped. parent = the overlay screen root.
         * Use it to push a picker/modal as a child of parent.             */
        void (*on_tap)(lv_obj_t *parent);
    } settings_row_t;

    /* A named group of rows (renders as a section in the settings list) */
    typedef struct
    {
        const char *title; /* Section header, e.g. "WiFi" — NULL = no header */
        const settings_row_t *rows;
        int count;
    } settings_group_t;

#ifdef __cplusplus
}
#endif