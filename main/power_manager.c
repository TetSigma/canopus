/**
 * power_manager.c — Display dim / light sleep / deep sleep
 *
 * State machine runs in a dedicated FreeRTOS task (low priority).
 * Touch events reset the inactivity counter from any task via
 * power_manager_touch() — thread-safe, no mutex needed (atomic write).
 *
 * Display on/off via AXP2101 ALDO1 rail (1.8V display power).
 * LVGL suspended via lv_timer_pause/resume on the LVGL tick timer.
 */

#include "power_manager.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <string.h>

static const char *TAG = "power_mgr";

/* ── AXP2101 display rail ────────────────────────────────────────── */
#define AXP2101_ADDR 0x34
#define AXP2101_ALDO_EN 0x90
#define AXP2101_ALDO1_EN (1 << 0) /* ALDO1 = 1.8V display power  */

/* ── State ───────────────────────────────────────────────────────── */
static volatile pm_state_t s_state = PM_STATE_AWAKE;
static volatile uint32_t s_last_touch_tick = 0;
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static int s_tp_int_pin = -1;
static esp_lcd_panel_handle_t s_panel = NULL;

/* ── AXP2101 helpers ─────────────────────────────────────────────── */
static uint8_t axp_read_reg(uint8_t reg)
{
    i2c_master_dev_handle_t dev = NULL;
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDR,
        .scl_speed_hz = 400000,
    };
    if (i2c_master_bus_add_device(s_i2c_bus, &cfg, &dev) != ESP_OK)
        return 0;
    uint8_t val = 0;
    i2c_master_transmit(dev, &reg, 1, pdMS_TO_TICKS(50));
    i2c_master_receive(dev, &val, 1, pdMS_TO_TICKS(50));
    i2c_master_bus_rm_device(dev);
    return val;
}

static void axp_write_reg(uint8_t reg, uint8_t val)
{
    i2c_master_dev_handle_t dev = NULL;
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDR,
        .scl_speed_hz = 400000,
    };
    if (i2c_master_bus_add_device(s_i2c_bus, &cfg, &dev) != ESP_OK)
        return;
    uint8_t buf[2] = {reg, val};
    i2c_master_transmit(dev, buf, 2, pdMS_TO_TICKS(50));
    i2c_master_bus_rm_device(dev);
}

static void display_power(bool on)
{
    /* 1. Tell the CO5300 controller to turn on/off */
    if (s_panel)
        esp_lcd_panel_disp_on_off(s_panel, on);

    /* 2. Cut/restore ALDO1 power rail via AXP2101 */
    uint8_t val = axp_read_reg(AXP2101_ALDO_EN);
    if (on)
        val |= AXP2101_ALDO1_EN;
    else
        val &= ~AXP2101_ALDO1_EN;
    axp_write_reg(AXP2101_ALDO_EN, val);

    ESP_LOGI(TAG, "display %s", on ? "ON" : "OFF");
}

/* ── LVGL suspend/resume ─────────────────────────────────────────── */
static void lvgl_suspend(void)
{
    /* Pause the LVGL port timer so the LVGL task stops waking the CPU */
    if (lvgl_port_lock(pdMS_TO_TICKS(200)))
    {
        lv_timer_pause(lv_timer_get_next(NULL)); /* pause first timer = tick */
        lvgl_port_unlock();
    }
}

static void lvgl_resume(void)
{
    if (lvgl_port_lock(pdMS_TO_TICKS(200)))
    {
        lv_timer_t *t = lv_timer_get_next(NULL);
        if (t)
            lv_timer_resume(t);
        lv_obj_invalidate(lv_screen_active()); /* force full redraw */
        lvgl_port_unlock();
    }
}

/* Forward declarations */
static void enter_deep_sleep(void);
static void do_wake(void);

/* ── Enter DIM ───────────────────────────────────────────────────── */
static void enter_dim(void)
{
    if (s_state == PM_STATE_DIM)
        return;
    ESP_LOGI(TAG, "→ DIM");
    s_state = PM_STATE_DIM;
    display_power(false);
    lvgl_suspend();
}

/* ── Enter LIGHT SLEEP ───────────────────────────────────────────── */
static void enter_light_sleep(void)
{
    ESP_LOGI(TAG, "→ LIGHT SLEEP");
    s_state = PM_STATE_LIGHT_SLEEP;
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);

    /* Wake every 100ms to poll touch — fast enough to catch a single tap */
    esp_sleep_enable_timer_wakeup(100 * 1000ULL); /* 100ms */

    while (s_state == PM_STATE_LIGHT_SLEEP)
    {
        esp_light_sleep_start();

        /* Check how long we've been idle total */
        uint32_t idle_ms = (xTaskGetTickCount() - s_last_touch_tick) * portTICK_PERIOD_MS;

        /* Read touch INT pin — low = touched (active low) */
        bool touched = (gpio_get_level(s_tp_int_pin) == 0);

        if (touched || idle_ms < (uint32_t)(PM_SLEEP_TIMEOUT_S * 1000))
        {
            /* Touch detected or timer reset elsewhere — wake up */
            break;
        }

        if (idle_ms >= (uint32_t)(PM_DEEP_SLEEP_TIMEOUT_S * 1000))
        {
            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
            esp_wifi_set_ps(WIFI_PS_NONE);
            enter_deep_sleep();
            return; /* never reached */
        }

        /* No touch, not yet deep sleep timeout — sleep again */
        esp_sleep_enable_timer_wakeup(100 * 1000ULL);
    }

    ESP_LOGI(TAG, "wake from light sleep");
    s_state = PM_STATE_AWAKE;
    esp_wifi_set_ps(WIFI_PS_NONE);
    display_power(true);
    vTaskDelay(pdMS_TO_TICKS(80)); /* display rail stabilise */
    lvgl_resume();
    s_last_touch_tick = xTaskGetTickCount();
}

/* ── Enter DEEP SLEEP ────────────────────────────────────────────── */
static void enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "→ DEEP SLEEP");
    s_state = PM_STATE_DEEP_SLEEP;

    /* Turn everything off cleanly */
    esp_wifi_stop();
    display_power(false);

    /* Wake only on touch (EXT1 — any low GPIO in the set) */
    esp_sleep_enable_ext1_wakeup(1ULL << s_tp_int_pin,
                                 ESP_EXT1_WAKEUP_ANY_LOW);

    esp_deep_sleep_start();
    /* Never returns — boot sequence runs on wake */
}

/* ── Wake from any sleep ─────────────────────────────────────────── */
static void do_wake(void)
{
    if (s_state == PM_STATE_AWAKE)
        return;
    ESP_LOGI(TAG, "wake");
    s_last_touch_tick = xTaskGetTickCount(); /* reset before display on */
    s_state = PM_STATE_AWAKE;
    display_power(true);
    vTaskDelay(pdMS_TO_TICKS(50));
    lvgl_resume();
}

/* ── Power manager task ──────────────────────────────────────────── */
static void pm_task(void *arg)
{
    s_last_touch_tick = xTaskGetTickCount();

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(100)); /* check 10x per second */

        uint32_t idle_ms = (xTaskGetTickCount() - s_last_touch_tick) * portTICK_PERIOD_MS;
        uint32_t idle_s = idle_ms / 1000;

        if (s_state == PM_STATE_AWAKE)
        {
            if (idle_s >= PM_DIM_TIMEOUT_S)
                enter_dim();
        }
        else if (s_state == PM_STATE_DIM)
        {
            /* Poll touch INT pin during DIM */
            if (gpio_get_level(s_tp_int_pin) == 0)
            {
                ESP_LOGI(TAG, "touch during DIM — waking");
                do_wake();
                continue;
            }
            if (idle_s >= PM_SLEEP_TIMEOUT_S)
                enter_light_sleep();
        }
        else if (s_state == PM_STATE_LIGHT_SLEEP)
        {
            /* handled inside enter_light_sleep loop */
        }
    }
}

/* ── Public API ──────────────────────────────────────────────────── */

esp_err_t power_manager_init(i2c_master_bus_handle_t i2c_bus,
                             int tp_int_pin,
                             esp_lcd_panel_handle_t panel)
{
    s_i2c_bus = i2c_bus;
    s_tp_int_pin = tp_int_pin;
    s_panel = panel;
    s_state = PM_STATE_AWAKE;
    s_last_touch_tick = xTaskGetTickCount();

    /* Configure touch INT pin as input for wakeup */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << tp_int_pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    xTaskCreate(pm_task, "pm_task", 6144, NULL, 1, NULL);
    ESP_LOGI(TAG, "init ok — dim=%ds sleep=%ds deep=%ds",
             PM_DIM_TIMEOUT_S, PM_SLEEP_TIMEOUT_S, PM_DEEP_SLEEP_TIMEOUT_S);
    return ESP_OK;
}

void power_manager_touch(void)
{
    s_last_touch_tick = xTaskGetTickCount();
    if (s_state == PM_STATE_DIM)
        do_wake();
    /* For LIGHT_SLEEP: the poll loop in enter_light_sleep checks
     * s_last_touch_tick after each 500ms wake and breaks out */
}

pm_state_t power_manager_state(void)
{
    return s_state;
}

void power_manager_wake(void)
{
    do_wake();
}