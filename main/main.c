/**
 * main.c — Waveshare ESP32-S3-Touch-AMOLED-1.75
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_lvgl_port.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lvgl.h"
#include "esp_log.h"
#include "app_manager.h"
#include "clock_service.h"
#include "power_manager.h"
#include "imu_service.h"
#include "imu_dashboard.h"
#include "imu_service.h"

/* ── Screen externs ──────────────────────────────────────────────── */
extern const screen_t screen_watchface;
extern const screen_t screen_menu;
extern const screen_t screen_wifi;
extern const screen_t screen_settings; /* ← added */

static const char *TAG = "main";

#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK 38
#define LCD_CS 12
#define LCD_RESET 39
#define LCD_WIDTH 466
#define LCD_HEIGHT 466

#define IIC_SDA 15
#define IIC_SCL 14
#define I2C_FREQ_HZ 400000

#define TP_INT 11
#define TP_RESET 40

#define LCD_SPI_HOST SPI2_HOST
#define LVGL_BUF_LINES 50

#define AXP2101_ADDR 0x34
#define AXP2101_DCDC_EN 0x80
#define AXP2101_DCDC1_EN (1 << 0)
#define AXP2101_ALDO1_VOL 0x92
#define AXP2101_ALDO3_VOL 0x94
#define AXP2101_ALDO4_VOL 0x95
#define AXP2101_ALDO_EN 0x90
#define AXP2101_ALDO1_EN (1 << 0)
#define AXP2101_ALDO3_EN (1 << 2)
#define AXP2101_ALDO4_EN (1 << 3)

i2c_master_bus_handle_t s_i2c_bus = NULL; /* extern'd by screen_settings */
SemaphoreHandle_t g_i2c_mutex = NULL;     /* extern'd by anyone doing I2C */
static i2c_master_dev_handle_t s_axp_dev = NULL;

static void axp_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    i2c_master_transmit(s_axp_dev, buf, 2, pdMS_TO_TICKS(50));
}

static uint8_t axp_read(uint8_t reg)
{
    uint8_t val = 0;
    i2c_master_transmit(s_axp_dev, &reg, 1, pdMS_TO_TICKS(50));
    i2c_master_receive(s_axp_dev, &val, 1, pdMS_TO_TICKS(50));
    return val;
}

static void axp2101_power_on_all(void)
{
    axp_write(AXP2101_DCDC_EN, axp_read(AXP2101_DCDC_EN) | AXP2101_DCDC1_EN);
    axp_write(AXP2101_ALDO1_VOL, 0x0D);
    axp_write(AXP2101_ALDO3_VOL, 0x19);
    axp_write(AXP2101_ALDO4_VOL, 0x0D);
    axp_write(AXP2101_ALDO_EN,
              axp_read(AXP2101_ALDO_EN) |
                  AXP2101_ALDO1_EN | AXP2101_ALDO3_EN | AXP2101_ALDO4_EN);
    ESP_LOGI(TAG, "AXP2101: DCDC1 (3.3V) on");
    ESP_LOGI(TAG, "AXP2101: ALDO1/3/4 on (display + touch)");
    vTaskDelay(pdMS_TO_TICKS(30));
}

static void init_i2c(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = IIC_SDA,
        .scl_io_num = IIC_SCL,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_i2c_bus));
    g_i2c_mutex = xSemaphoreCreateMutex();
    configASSERT(g_i2c_mutex);

    const i2c_device_config_t axp_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c_bus, &axp_cfg, &s_axp_dev));
    ESP_LOGI(TAG, "I2C ready");
}

static lv_indev_t *s_indev = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;

static lv_disp_t *init_display(void)
{
    const spi_bus_config_t buscfg =
        CO5300_PANEL_BUS_QSPI_CONFIG(
            LCD_SCLK,
            LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3,
            LCD_WIDTH * LVGL_BUF_LINES * sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg =
        CO5300_PANEL_IO_QSPI_CONFIG(LCD_CS, NULL, NULL);
    io_cfg.pclk_hz = 40 * 1000 * 1000;
    ESP_ERROR_CHECK(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,
                                 &io_cfg, &io_handle));

    esp_lcd_panel_handle_t panel = NULL;
    co5300_vendor_config_t vendor = {0};
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RESET,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(io_handle, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, 6, 0));
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    s_panel = panel; /* save for power manager */
    ESP_LOGI(TAG, "Display on (%dx%d)", LCD_WIDTH, LCD_HEIGHT);

    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 5,
        .task_stack = 16384,
        .task_affinity = 1,
        .task_max_sleep_ms = 10,
        .timer_period_ms = 1,
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel,
        .buffer_size = LCD_WIDTH * LVGL_BUF_LINES,
        .double_buffer = true,
        .hres = LCD_WIDTH,
        .vres = LCD_HEIGHT,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {.mirror_x = false, .mirror_y = false},
        .flags = {.buff_dma = true, .buff_spiram = false, .swap_bytes = true},
    };
    lv_disp_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (!disp)
    {
        ESP_LOGE(TAG, "lvgl_port_add_disp failed");
        abort();
    }
    ESP_LOGI(TAG, "LVGL display registered");
    return disp;
}

lv_indev_t *get_touch_indev(void) { return s_indev; }

static void touch_event_cb(lv_event_t *e)
{
    (void)e;
    power_manager_touch();
}

static void init_touch(lv_disp_t *disp)
{
    gpio_config_t rst_cfg = {
        .pin_bit_mask = (1ULL << TP_RESET),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&rst_cfg));
    gpio_set_level(TP_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TP_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    io_config.scl_speed_hz = I2C_FREQ_HZ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_config, &tp_io));

    esp_lcd_touch_handle_t tp = NULL;
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_WIDTH,
        .y_max = LCD_HEIGHT,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {.reset = 0, .interrupt = 0},
        .flags = {.swap_xy = 0, .mirror_x = 1, .mirror_y = 1},
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst9217(tp_io, &tp_cfg, &tp));
    ESP_LOGI(TAG, "CST9217 touch initialised");

    const lvgl_port_touch_cfg_t touch_cfg = {.disp = disp, .handle = tp};
    lv_indev_t *indev = lvgl_port_add_touch(&touch_cfg);
    if (!indev)
    {
        ESP_LOGE(TAG, "lvgl_port_add_touch failed");
        abort();
    }
    s_indev = indev;
    ESP_LOGI(TAG, "Touch registered via lvgl_port");

    /* Notify power manager on every touch so it resets inactivity timer */
    lv_indev_add_event_cb(indev, touch_event_cb, LV_EVENT_PRESSED, NULL);
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    ESP_LOGI(TAG, "Got IP — syncing clock");
    clock_service_sync();
    imu_dashboard_start();
}

static void wifi_init_task(void *arg)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    ESP_LOGI(TAG, "WiFi STA started");

    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_got_ip, NULL);

    char ssid[33] = {0}, pass[65] = {0};
    size_t ssid_len = sizeof(ssid), pass_len = sizeof(pass);

    nvs_handle_t h;
    if (nvs_open("wifi_cfg", NVS_READONLY, &h) == ESP_OK)
    {
        bool ok = (nvs_get_str(h, "ssid", ssid, &ssid_len) == ESP_OK) &&
                  (nvs_get_str(h, "pass", pass, &pass_len) == ESP_OK);
        nvs_close(h);
        if (ok && strlen(ssid) > 0)
        {
            ESP_LOGI(TAG, "Auto-connecting to '%s'", ssid);
            wifi_config_t wcfg = {0};
            memcpy(wcfg.sta.ssid, ssid, sizeof(wcfg.sta.ssid) - 1);
            memcpy(wcfg.sta.password, pass, sizeof(wcfg.sta.password) - 1);
            wcfg.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
            wcfg.sta.pmf_cfg.capable = false;
            wcfg.sta.pmf_cfg.required = false;
            esp_wifi_set_config(WIFI_IF_STA, &wcfg);
            esp_wifi_connect();
        }
    }
    vTaskDelete(NULL);
}

static void register_screens(void)
{
    ESP_ERROR_CHECK(app_manager_register(&screen_watchface));
    ESP_ERROR_CHECK(app_manager_register(&screen_menu));
    ESP_ERROR_CHECK(app_manager_register_overlay(&screen_wifi));
    ESP_ERROR_CHECK(app_manager_register_overlay(&screen_settings)); /* ← added */
}

void app_main(void)
{
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }
    ESP_LOGI(TAG, "NVS ready");

    init_i2c();
    axp2101_power_on_all();

    lv_disp_t *disp = init_display();
    init_touch(disp);

    ESP_ERROR_CHECK(app_manager_init());
    register_screens();

    clock_service_init();
    xTaskCreate(wifi_init_task, "wifi_init", 8192, NULL, 2, NULL);

    /* 10. IMU service — double-tap to wake */
    imu_service_init(s_i2c_bus, power_manager_wake);

    /* 11. Power manager — dim/sleep after inactivity */
    power_manager_init(s_i2c_bus, TP_INT, s_panel);

    /* 11. IMU service — wake on motion (wrist raise / tap) */
    imu_service_init(s_i2c_bus, power_manager_wake);

    ESP_LOGI(TAG, "Boot complete");
}