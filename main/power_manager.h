#pragma once

/**
 * power_manager.h — Display dim, light sleep, deep sleep
 *
 * Three states:
 *
 *   AWAKE      Normal operation. Display on, LVGL running.
 *              Inactivity timer ticking.
 *
 *   DIM        After DIM_TIMEOUT_S seconds of no touch.
 *              Display off (ALDO1 rail killed via AXP2101).
 *              LVGL timer paused — zero CPU for rendering.
 *              CPU still running (WiFi, clock).
 *              Wake: any touch → instant resume to AWAKE.
 *
 *   LIGHT_SLEEP After SLEEP_TIMEOUT_S seconds in DIM state.
 *              CPU halted (esp_light_sleep_start).
 *              RAM retained — full state preserved.
 *              WiFi modem sleep active.
 *              Wake source: TP_INT (touch interrupt) or RTC timer.
 *              Wake latency: ~1ms.
 *
 *   DEEP_SLEEP  After DEEP_SLEEP_TIMEOUT_S seconds in LIGHT_SLEEP.
 *              Full power off. RAM lost.
 *              Wake source: TP_INT only (EXT1 wakeup).
 *              Full reinit on wake (boot sequence runs again).
 *              Use only if long idle expected.
 */

#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ── Timeouts (seconds) ──────────────────────────────────────────── */
#define PM_DIM_TIMEOUT_S 10         /* no touch → dim display      */
#define PM_SLEEP_TIMEOUT_S 30       /* dim → light sleep           */
#define PM_DEEP_SLEEP_TIMEOUT_S 120 /* light sleep → deep sleep    */

    typedef enum
    {
        PM_STATE_AWAKE,
        PM_STATE_DIM,
        PM_STATE_LIGHT_SLEEP,
        PM_STATE_DEEP_SLEEP, /* entered but never returned from */
    } pm_state_t;

    /**
     * Init power manager.
     * @param i2c_bus     shared I2C bus (for AXP2101)
     * @param tp_int_pin  touch interrupt GPIO (wakeup source)
     * @param panel       LCD panel handle (for display on/off command)
     */
    esp_err_t power_manager_init(i2c_master_bus_handle_t i2c_bus,
                                 int tp_int_pin,
                                 esp_lcd_panel_handle_t panel);

    /** Call on every touch event to reset the inactivity timer */
    void power_manager_touch(void);

    /** Returns current power state */
    pm_state_t power_manager_state(void);

    /** Force immediate wake (e.g. on incoming notification) */
    void power_manager_wake(void);

#ifdef __cplusplus
}
#endif