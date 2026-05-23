/**
 * ui_kit.c — Reusable LVGL component implementations
 */

#include "ui_kit.h"
#include "ui_theme.h"
#include <stdio.h>
#include <string.h>

/* ── Internal helpers ────────────────────────────────────────────── */

/* Remove all focus/pressed visual artefacts from any clickable object */
static void strip_focus(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_outline_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(obj, 0, LV_STATE_FOCUSED | LV_PART_MAIN);
    lv_obj_set_style_outline_width(obj, 0, LV_STATE_FOCUS_KEY | LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_STATE_PRESSED | LV_PART_MAIN);
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *label,
                             lv_color_t bg, int radius)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_height(btn, UK_BTN_H);
    lv_obj_set_style_bg_color(btn, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, radius, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, UK_COL_ROW_PRESS,
                              LV_STATE_PRESSED | LV_PART_MAIN);
    strip_focus(btn);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, UK_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, UK_COL_WHITE, LV_PART_MAIN);
    lv_obj_center(lbl);

    return btn;
}

/* ═══════════════════════════════════════════════════════════════════
 * Buttons
 * ═══════════════════════════════════════════════════════════════════ */

lv_obj_t *uk_button(lv_obj_t *parent, const char *label, lv_color_t bg)
{
    return make_button(parent, label, bg, UK_BTN_RADIUS);
}

lv_obj_t *uk_button_danger(lv_obj_t *parent, const char *label)
{
    return make_button(parent, label, UK_COL_DANGER, UK_BTN_RADIUS);
}

lv_obj_t *uk_button_ghost(lv_obj_t *parent, const char *label)
{
    return make_button(parent, label, UK_COL_BTN, UK_BTN_RADIUS);
}

/* ═══════════════════════════════════════════════════════════════════
 * Labels
 * ═══════════════════════════════════════════════════════════════════ */

lv_obj_t *uk_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, UK_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, UK_COL_WHITE, LV_PART_MAIN);
    return lbl;
}

lv_obj_t *uk_label_muted(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, UK_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, UK_COL_MUTED, LV_PART_MAIN);
    return lbl;
}

lv_obj_t *uk_label_section(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, UK_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, UK_COL_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(lbl, 2, LV_PART_MAIN);
    return lbl;
}

/* ═══════════════════════════════════════════════════════════════════
 * Settings rows
 * ═══════════════════════════════════════════════════════════════════ */

lv_obj_t *uk_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, UK_ROW_W, UK_ROW_H);
    lv_obj_set_style_bg_color(row, UK_COL_ROW, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(row, UK_ROW_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_pad_left(row, UK_PAD_H, LV_PART_MAIN);
    lv_obj_set_style_pad_right(row, UK_PAD_H, LV_PART_MAIN);
    lv_obj_set_style_pad_top(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, UK_COL_ROW_PRESS,
                              LV_STATE_PRESSED | LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    strip_focus(row);
    return row;
}

lv_obj_t *uk_row_titled(lv_obj_t *parent, const char *icon, const char *title)
{
    lv_obj_t *row = uk_row(parent);

    char buf[64];
    if (icon && icon[0])
        snprintf(buf, sizeof(buf), "%s  %s", icon, title);
    else
        snprintf(buf, sizeof(buf), "%s", title);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_font(lbl, UK_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, UK_COL_WHITE, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

    return row;
}

lv_obj_t *uk_row_set_value(lv_obj_t *row, const char *value, lv_color_t color)
{
    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, value);
    lv_obj_set_style_text_font(val, UK_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(val, color, LV_PART_MAIN);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, 0, 0);
    return val;
}

/* ═══════════════════════════════════════════════════════════════════
 * Containers
 * ═══════════════════════════════════════════════════════════════════ */

lv_obj_t *uk_list(lv_obj_t *parent)
{
    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, UK_ROW_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    return list;
}

lv_obj_t *uk_overlay(lv_obj_t *parent)
{
    lv_obj_t *ov = lv_obj_create(parent);
    lv_obj_set_size(ov, LCD_HOR_RES, LCD_VER_RES);
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_style_bg_color(ov, UK_COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ov, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(ov, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ov, 0, LV_PART_MAIN);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    return ov;
}

lv_obj_t *uk_modal_card(lv_obj_t *overlay)
{
    lv_obj_t *card = lv_obj_create(overlay);
    lv_obj_set_size(card, UK_MODAL_W, UK_MODAL_H);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, UK_COL_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(card, UK_CARD_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

/* ═══════════════════════════════════════════════════════════════════
 * Screen helpers
 * ═══════════════════════════════════════════════════════════════════ */

void uk_screen_bg(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, UK_COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(parent, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(parent, 0, LV_PART_MAIN);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *uk_screen_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, UK_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, UK_COL_WHITE, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, UK_TITLE_Y);
    return lbl;
}