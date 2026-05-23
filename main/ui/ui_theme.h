#pragma once

/**
 * ui_theme.h — All magic numbers in one place.
 * Change here, affects every screen.
 */

#include "lvgl.h"
#include "screen.h" /* LCD_HOR_RES, LCD_VER_RES */

/* ── Fonts ───────────────────────────────────────────────────────── */
#define UK_FONT (&lv_font_montserrat_14)
#define UK_FONT_LARGE (&lv_font_montserrat_24)

/* ── Colors ──────────────────────────────────────────────────────── */
#define UK_COL_BG lv_color_black()
#define UK_COL_ROW lv_color_make(0x1C, 0x1C, 0x1C)
#define UK_COL_ROW_PRESS lv_color_make(0x2C, 0x2C, 0x2C)
#define UK_COL_CARD lv_color_make(0x22, 0x22, 0x22)
#define UK_COL_BTN lv_color_make(0x33, 0x33, 0x33)
#define UK_COL_DANGER lv_color_make(0xCC, 0x22, 0x22)
#define UK_COL_ACCENT lv_color_make(0x22, 0x88, 0xFF)
#define UK_COL_WHITE lv_color_white()
#define UK_COL_MUTED lv_color_make(0x88, 0x88, 0x88)
#define UK_COL_DIM lv_color_make(0x66, 0x66, 0x66)
#define UK_COL_GREEN lv_color_make(0x22, 0xDD, 0x22)
#define UK_COL_RED lv_color_make(0xFF, 0x44, 0x44)

/* ── Sizes ───────────────────────────────────────────────────────── */
#define UK_ROW_W 360
#define UK_ROW_H 56
#define UK_BTN_H 44
#define UK_BTN_RADIUS 22 /* pill shape */
#define UK_ROW_RADIUS 10
#define UK_CARD_RADIUS 16
#define UK_MODAL_W 340
#define UK_MODAL_H 210

/* ── Layout ──────────────────────────────────────────────────────── */
#define UK_TITLE_Y 18 /* y offset for screen titles */
#define UK_LIST_Y 55  /* y offset for list below title */
#define UK_ROW_GAP 8  /* gap between rows */
#define UK_PAD_H 16   /* horizontal padding inside rows */