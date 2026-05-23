#pragma once

/**
 * ui_kit.h — Reusable LVGL components for the smartwatch UI
 *
 * All functions return the primary lv_obj_t you need to position/use.
 * Styling is baked in — no need to set colors, fonts, borders per screen.
 *
 * Theme constants are in ui_theme.h — change once, affects everywhere.
 */

#include "lvgl.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ── Buttons ─────────────────────────────────────────────────────── */

    /** Standard pill button with custom background color */
    lv_obj_t *uk_button(lv_obj_t *parent, const char *label, lv_color_t bg);

    /** Danger button — red, for destructive actions */
    lv_obj_t *uk_button_danger(lv_obj_t *parent, const char *label);

    /** Ghost button — dark grey, for cancel/secondary actions */
    lv_obj_t *uk_button_ghost(lv_obj_t *parent, const char *label);

    /* ── Labels ──────────────────────────────────────────────────────── */

    /** Primary white label */
    lv_obj_t *uk_label(lv_obj_t *parent, const char *text);

    /** Muted label — grey */
    lv_obj_t *uk_label_muted(lv_obj_t *parent, const char *text);

    /** Section header — dim grey, letter spaced */
    lv_obj_t *uk_label_section(lv_obj_t *parent, const char *text);

    /* ── Settings rows ───────────────────────────────────────────────── */

    /** Bare row — dark bg, rounded, no focus rect. Add clickable + event yourself. */
    lv_obj_t *uk_row(lv_obj_t *parent);

    /** Row with icon + title already added left-aligned */
    lv_obj_t *uk_row_titled(lv_obj_t *parent, const char *icon, const char *title);

    /** Add right-aligned value label to existing row. Returns the label. */
    lv_obj_t *uk_row_set_value(lv_obj_t *row, const char *value, lv_color_t color);

    /* ── Containers ──────────────────────────────────────────────────── */

    /** Scrollable vertical flex column, transparent bg, no scroll chain */
    lv_obj_t *uk_list(lv_obj_t *parent);

    /** Full-screen black overlay on top of parent */
    lv_obj_t *uk_overlay(lv_obj_t *parent);

    /** Centred modal card (340x210) — put on top of uk_overlay() */
    lv_obj_t *uk_modal_card(lv_obj_t *overlay);

    /* ── Screen helpers ──────────────────────────────────────────────── */

    /** Black bg + no scroll on parent */
    void uk_screen_bg(lv_obj_t *parent);

    /** Centred title label near top. Returns the label. */
    lv_obj_t *uk_screen_title(lv_obj_t *parent, const char *text);

#ifdef __cplusplus
}
#endif