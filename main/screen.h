#pragma once

/**
 * screen.h — tileview edition
 *
 * Each screen is a tile inside a fullscreen lv_tileview.
 * create(parent) builds the UI as children of the given tile object.
 * No lv_scr_load(), no keep_alive flag — tiles always stay in memory.
 *
 * NAMING RULE: prefix all static functions with the screen name.
 *   watchface_create(), watchface_tick() — NOT create(), tick()
 *   (on_exit conflicts with POSIX stdlib.h)
 */

#include "lvgl.h"
#include <stdbool.h>

#define LCD_HOR_RES 466
#define LCD_VER_RES 466

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct screen_t
    {
        const char *name;

        /**
         * create(parent)
         * Build the screen UI as children of `parent` (the tile object).
         * Do NOT call lv_scr_load() — the tileview handles display.
         * Called once at registration time with the LVGL lock held.
         */
        void (*create)(lv_obj_t *parent);

        /**
         * on_enter() — tile scrolled into view and settled. May be NULL.
         * on_exit()  — tile scrolled away and settled.      May be NULL.
         * tick()     — called every APP_MANAGER_TICK_MS while active. May be NULL.
         * All called with the LVGL lock held.
         */
        void (*on_enter)(void);
        void (*on_exit)(void);
        void (*tick)(void);
    } screen_t;

#ifdef __cplusplus
}
#endif