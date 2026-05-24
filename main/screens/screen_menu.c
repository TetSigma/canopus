/**
 * screen_menu.c — Honeycomb hex menu with symbol icons
 */

#include "screen.h"
#include "app_manager.h"
#include "clock_service.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "scr_menu";

#define HEX_R 46
#define HEX_GAP 16
#define HEX_STEP ((int32_t)(1.732050808f * (HEX_R + HEX_GAP)))
#define HIT_SIZE (HEX_R * 2 + 28)
#define ICON_PAD 16
#define RING_R (LCD_HOR_RES / 2 - 14)
#define RING_TICKS 36
#define CX (LCD_HOR_RES / 2)
#define CY (LCD_VER_RES / 2 + 16)

typedef struct
{
    const char *icon;
    const char *screen_name;
    bool center;
} cell_t;

static const cell_t CELLS[] = {
    {LV_SYMBOL_HOME, NULL, true},
    {LV_SYMBOL_WIFI, "wifi", false},
    {LV_SYMBOL_CALL, NULL, false},
    {LV_SYMBOL_PLAY, NULL, false},
    {LV_SYMBOL_IMAGE, NULL, false},
    {LV_SYMBOL_SETTINGS, "settings", false},
    {LV_SYMBOL_LIST, NULL, false},
};
#define CELL_COUNT ((int)(sizeof(CELLS) / sizeof(CELLS[0])))

static lv_obj_t *s_time_label = NULL;
static int s_last_min = -1;
static uint8_t *s_canvas_buf = NULL;

/* ── Math ────────────────────────────────────────────────────────── */
static void hex_pts(int32_t cx, int32_t cy, int32_t r,
                    lv_point_precise_t p[6])
{
    for (int i = 0; i < 6; i++)
    {
        double a = (i * 60.0 - 30.0) * M_PI / 180.0;
        lv_point_precise_set(&p[i],
                             cx + (int32_t)(r * cos(a)),
                             cy + (int32_t)(r * sin(a)));
    }
}

static void cell_pos(int idx, int32_t *ox, int32_t *oy)
{
    if (idx == 0)
    {
        *ox = CX;
        *oy = CY;
        return;
    }
    double a = ((idx - 1) * 60.0 - 90.0) * M_PI / 180.0;
    *ox = CX + (int32_t)(HEX_STEP * cos(a));
    *oy = CY + (int32_t)(HEX_STEP * sin(a));
}

/* ── Hex draw ────────────────────────────────────────────────────── */
static void draw_hex(lv_layer_t *layer,
                     int32_t cx, int32_t cy, int32_t r,
                     lv_color_t fill, lv_opa_t fill_opa,
                     lv_color_t border, int border_w)
{
    lv_point_precise_t pts[6];
    hex_pts(cx, cy, r, pts);

    lv_draw_triangle_dsc_t tri;
    lv_draw_triangle_dsc_init(&tri);
    tri.color = fill;
    tri.opa = fill_opa;

    lv_point_precise_t centre;
    lv_point_precise_set(&centre, cx, cy);

    for (int i = 0; i < 6; i++)
    {
        lv_point_precise_t p1 = pts[i];
        lv_point_precise_t p2 = pts[(i + 1) % 6];
        int32_t dx1 = (int32_t)p1.x - cx, dy1 = (int32_t)p1.y - cy;
        int32_t dx2 = (int32_t)p2.x - cx, dy2 = (int32_t)p2.y - cy;
        if (dx1 > 0)
            p1.x++;
        else if (dx1 < 0)
            p1.x--;
        if (dy1 > 0)
            p1.y++;
        else if (dy1 < 0)
            p1.y--;
        if (dx2 > 0)
            p2.x++;
        else if (dx2 < 0)
            p2.x--;
        if (dy2 > 0)
            p2.y++;
        else if (dy2 < 0)
            p2.y--;
        tri.p[0] = centre;
        tri.p[1] = p1;
        tri.p[2] = p2;
        lv_draw_triangle(layer, &tri);
    }

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = border;
    line.width = border_w;
    line.opa = fill_opa;
    line.round_start = 1;
    line.round_end = 1;
    for (int i = 0; i < 6; i++)
    {
        line.p1 = pts[i];
        line.p2 = pts[(i + 1) % 6];
        lv_draw_line(layer, &line);
    }
}

/* ── Canvas render ───────────────────────────────────────────────── */
static void render_canvas(lv_obj_t *canvas)
{
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    lv_draw_rect_dsc_t bg;
    lv_draw_rect_dsc_init(&bg);
    bg.bg_color = lv_color_black();
    bg.bg_opa = LV_OPA_COVER;
    lv_area_t full = {0, 0, LCD_HOR_RES - 1, LCD_VER_RES - 1};
    lv_draw_rect(&layer, &bg, &full);

    lv_draw_line_dsc_t tick;
    lv_draw_line_dsc_init(&tick);
    tick.color = lv_color_make(0x33, 0x33, 0x33);
    tick.width = 1;
    tick.opa = LV_OPA_COVER;
    for (int i = 0; i < RING_TICKS; i++)
    {
        double a = (i * 10.0 - 90.0) * M_PI / 180.0;
        int32_t r0 = RING_R, r1 = RING_R - ((i % 6) == 0 ? 10 : 5);
        lv_point_precise_set(&tick.p1,
                             LCD_HOR_RES / 2 + (int32_t)(r0 * cos(a)),
                             LCD_VER_RES / 2 + (int32_t)(r0 * sin(a)));
        lv_point_precise_set(&tick.p2,
                             LCD_HOR_RES / 2 + (int32_t)(r1 * cos(a)),
                             LCD_VER_RES / 2 + (int32_t)(r1 * sin(a)));
        lv_draw_line(&layer, &tick);
    }

    for (int i = 0; i < CELL_COUNT; i++)
    {
        int32_t cx, cy;
        cell_pos(i, &cx, &cy);
        const cell_t *c = &CELLS[i];
        lv_color_t fill = c->center        ? lv_color_make(0x50, 0x50, 0x50)
                          : c->screen_name ? lv_color_make(0x2C, 0x2C, 0x2C)
                                           : lv_color_make(0x1C, 0x1C, 0x1C);
        lv_color_t border = c->center ? lv_color_white()
                                      : lv_color_make(0x66, 0x66, 0x66);
        lv_opa_t opa = (c->screen_name || c->center) ? LV_OPA_COVER : LV_OPA_50;
        draw_hex(&layer, cx, cy, HEX_R, fill, opa, border, c->center ? 2 : 1);
    }

    lv_draw_label_dsc_t icon_dsc;
    lv_draw_label_dsc_init(&icon_dsc);
    icon_dsc.font = &lv_font_montserrat_24;
    icon_dsc.align = LV_TEXT_ALIGN_CENTER;
    for (int i = 0; i < CELL_COUNT; i++)
    {
        int32_t cx, cy;
        cell_pos(i, &cx, &cy);
        const cell_t *c = &CELLS[i];
        icon_dsc.color = lv_color_white();
        icon_dsc.opa = (c->screen_name || c->center) ? LV_OPA_COVER : LV_OPA_40;
        icon_dsc.text = c->icon;
        lv_area_t ia = {cx - HEX_R, cy - ICON_PAD, cx + HEX_R, cy + ICON_PAD};
        lv_draw_label(&layer, &icon_dsc, &ia);
    }

    lv_draw_label_dsc_t foot_dsc;
    lv_draw_label_dsc_init(&foot_dsc);
    foot_dsc.font = &lv_font_montserrat_14;
    foot_dsc.align = LV_TEXT_ALIGN_CENTER;
    foot_dsc.color = lv_color_make(0x44, 0x44, 0x44);
    foot_dsc.opa = LV_OPA_COVER;
    foot_dsc.text = "MENU";
    lv_area_t fa = {0, LCD_VER_RES - 30, LCD_HOR_RES - 1, LCD_VER_RES - 1};
    lv_draw_label(&layer, &foot_dsc, &fa);

    lv_canvas_finish_layer(canvas, &layer);
}

/* ── Button callback — single event, no double fire ─────────────── */
static void btn_cb(lv_event_t *e)
{
    /* Guard against double-fire from gesture+click conflict */
    static uint32_t s_last_fire = 0;
    uint32_t now = lv_tick_get();
    if (now - s_last_fire < 500)
        return; /* debounce 500ms */
    s_last_fire = now;

    const char *scr = (const char *)lv_event_get_user_data(e);
    if (scr)
        app_manager_switch_to(scr);
}

/* ── create() ────────────────────────────────────────────────────── */
static void menu_create(lv_obj_t *parent)
{
    ESP_LOGI(TAG, "create");
    s_last_min = -1;

    lv_obj_set_style_bg_color(parent, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(parent, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(parent, 0, LV_PART_MAIN);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    size_t buf_sz = (size_t)LCD_HOR_RES * LCD_VER_RES * sizeof(lv_color_t);
    s_canvas_buf = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_canvas_buf)
        s_canvas_buf = heap_caps_malloc(buf_sz, MALLOC_CAP_8BIT);
    if (!s_canvas_buf)
    {
        ESP_LOGE(TAG, "canvas alloc failed");
        return;
    }

    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, s_canvas_buf,
                         LCD_HOR_RES, LCD_VER_RES, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas, 0, 0);
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    render_canvas(canvas);

    s_time_label = lv_label_create(parent);
    lv_label_set_text(s_time_label, "00:00");
#if LV_FONT_MONTSERRAT_48
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_48, LV_PART_MAIN);
#else
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_24, LV_PART_MAIN);
#endif
    lv_obj_set_style_text_color(s_time_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_time_label, -1, LV_PART_MAIN);
    lv_obj_set_width(s_time_label, LCD_HOR_RES);
    lv_obj_set_style_text_align(s_time_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_time_label, LV_ALIGN_TOP_MID, 0, 28);

    /* Tappable hex buttons — ONLY for cells with screen_name */
    for (int i = 0; i < CELL_COUNT; i++)
    {
        const cell_t *c = &CELLS[i];
        if (!c->screen_name)
            continue; /* skip non-interactive cells */

        int32_t cx, cy;
        cell_pos(i, &cx, &cy);

        lv_obj_t *btn = lv_obj_create(parent); /* plain obj, not lv_button */
        lv_obj_set_size(btn, HIT_SIZE, HIT_SIZE);
        lv_obj_set_pos(btn, cx - HIT_SIZE / 2, cy - HIT_SIZE / 2);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);

        /* Only LV_EVENT_CLICKED — not SHORT_CLICKED, avoids double fire */
        lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, (void *)c->screen_name);
    }
}

/* ── tick() ─────────────────────────────────────────────────────── */
static void menu_tick(void)
{
    if (!s_time_label)
        return;
    time_t now = clock_service_now();
    struct tm t;
    localtime_r(&now, &t);
    int stamp = t.tm_hour * 60 + t.tm_min;
    if (stamp == s_last_min)
        return;
    s_last_min = stamp;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    lv_label_set_text(s_time_label, buf);
}

static void menu_on_enter(void)
{
    const char *tz = getenv("TZ");
    if (tz)
    {
        setenv("TZ", tz, 1);
        tzset();
    }
    s_last_min = -1;
}

static void menu_on_exit(void)
{
    s_last_min = -1;
    /* Don't null s_time_label — the tile object persists and the label is still valid.
     * Only free the canvas buffer since it's heap-allocated separately. */
    if (s_canvas_buf)
    {
        heap_caps_free(s_canvas_buf);
        s_canvas_buf = NULL;
    }
}

const screen_t screen_menu = {
    .name = "menu",
    .create = menu_create,
    .on_enter = menu_on_enter,
    .on_exit = menu_on_exit,
    .tick = menu_tick,
};