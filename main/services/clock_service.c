/**
 * clock_service.c
 *
 * Boot sequence
 * ─────────────────────────────────────────────────────────────────
 *  1. clock_service_init()
 *     - Opens NVS, reads last saved unix timestamp
 *     - If found and sane (> year 2020), calls settimeofday() with it
 *     - System clock now shows last known time immediately at boot
 *
 *  2. WiFi connects → wifi_init_task calls clock_service_sync()
 *     - Spawns ntp_task (stack 4096, priority 2)
 *     - sntp_setoperatingmode + sntp_setservername + sntp_init
 *     - Waits up to 10 seconds for sync
 *     - On success: saves new timestamp to NVS, sets s_synced flag
 *     - On timeout: logs warning, keeps NVS time — no crash, no retry storm
 *
 * Timezone
 * ────────
 * Set CLOCK_SERVICE_TZ to your POSIX timezone string.
 * Examples:
 *   "UTC0"                          UTC
 *   "CET-1CEST,M3.5.0,M10.5.0/3"   Central European (Warsaw)
 *   "EST5EDT,M3.2.0,M11.1.0"        US Eastern
 *   "PST8PDT,M3.2.0,M11.1.0"        US Pacific
 */

#include "clock_service.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_netif_sntp.h" /* ESP-IDF v5.1+ unified SNTP header */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <sys/time.h>

static const char *TAG = "clock_svc";

/* ── Config ──────────────────────────────────────────────────────── */
#define CLOCK_SERVICE_TZ_DEFAULT "CET-1CEST,M3.5.0,M10.5.0/3" /* fallback */
#define NVS_NS_CLOCK "clock_svc"
#define NVS_KEY_TIMESTAMP "last_ts"
#define NVS_KEY_TZ "tz_name"
#define MIN_SANE_TIMESTAMP 1609459200 /* 2021-01-01 */
#define NVS_KEY_TZ "tz_name"

/* Load saved TZ from NVS, fall back to default */
static void apply_saved_tz(void)
{
    char tz_buf[64] = {0};
    size_t len = sizeof(tz_buf);
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CLOCK, NVS_READONLY, &h) == ESP_OK)
    {
        nvs_get_str(h, NVS_KEY_TZ, tz_buf, &len);
        nvs_close(h);
    }
    const char *tz = (strlen(tz_buf) > 0) ? tz_buf : CLOCK_SERVICE_TZ_DEFAULT;
    setenv("TZ", tz, 1);
    tzset();
    ESP_LOGI(TAG, "TZ = %s", tz);
}
#define NTP_SERVER_PRIMARY "pool.ntp.org"
#define NTP_TIMEOUT_MS 10000

/* ── State ───────────────────────────────────────────────────────── */
static volatile bool s_synced = false;

/* ── NVS helpers ─────────────────────────────────────────────────── */
static void nvs_save_time(time_t t)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CLOCK, NVS_READWRITE, &h) != ESP_OK)
        return;
    nvs_set_u32(h, NVS_KEY_TIMESTAMP, (uint32_t)t);
    nvs_commit(h);
    nvs_close(h);
}

static time_t nvs_load_time(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_CLOCK, NVS_READONLY, &h) != ESP_OK)
        return 0;
    uint32_t ts = 0;
    nvs_get_u32(h, NVS_KEY_TIMESTAMP, &ts);
    nvs_close(h);
    return (time_t)ts;
}

/* ── NTP task ────────────────────────────────────────────────────── */
static volatile bool s_syncing = false;

static void ntp_task(void *arg)
{
    ESP_LOGI(TAG, "NTP sync starting...");

    const esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER_PRIMARY);
    esp_netif_sntp_init(&config);

    /* Wait up to NTP_TIMEOUT_MS for sync */
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(NTP_TIMEOUT_MS)) == ESP_OK)
    {
        time_t now = time(NULL);
        ESP_LOGI(TAG, "NTP sync OK: %ld", (long)now);
        apply_saved_tz(); /* re-apply in case it changed since boot */
        nvs_save_time(now);
        s_synced = true;

        struct tm t;
        localtime_r(&now, &t);
        ESP_LOGI(TAG, "Local time: %02d:%02d %02d/%02d/%04d",
                 t.tm_hour, t.tm_min,
                 t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    }
    else
    {
        ESP_LOGW(TAG, "NTP sync timed out — using saved/NVS time");
    }

    esp_netif_sntp_deinit();
    s_syncing = false; /* allow retry on next IP event */
    vTaskDelete(NULL);
}

/* ── NVS persistence task — saves time every 60s ─────────────────── */
static void persist_task(void *arg)
{
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(60000));
        time_t now = time(NULL);
        if (now > MIN_SANE_TIMESTAMP)
        {
            nvs_save_time(now);
        }
    }
}

/* ── Public API ──────────────────────────────────────────────────── */

void clock_service_init(void)
{
    /* Apply saved timezone first */
    apply_saved_tz();

    /* Load last known time from NVS */
    time_t saved = nvs_load_time();
    if (saved > MIN_SANE_TIMESTAMP)
    {
        struct timeval tv = {.tv_sec = saved, .tv_usec = 0};
        settimeofday(&tv, NULL);
        struct tm t;
        localtime_r(&saved, &t);
        ESP_LOGI(TAG, "Restored time from NVS: %02d:%02d %02d/%02d/%04d",
                 t.tm_hour, t.tm_min,
                 t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    }
    else
    {
        ESP_LOGW(TAG, "No saved time in NVS — showing epoch until NTP syncs");
    }

    /* Periodically save current time so reboots lose at most 60s */
    xTaskCreate(persist_task, "clk_persist", 2048, NULL, 1, NULL);
}

void clock_service_sync(void)
{
    if (s_syncing)
    {
        ESP_LOGI(TAG, "NTP already in progress");
        return;
    }
    s_syncing = true;
    xTaskCreate(ntp_task, "ntp_sync", 4096, NULL, 2, NULL);
}

time_t clock_service_now(void)
{
    return time(NULL);
}

bool clock_service_is_synced(void)
{
    return s_synced;
}

static volatile int s_tz_changed = 0;

void clock_service_force_tick_refresh(void)
{
    s_tz_changed = 1;
}

bool clock_service_tz_changed(void)
{
    return s_tz_changed != 0;
}

void clock_service_apply_tz(void)
{
    if (!s_tz_changed)
        return;
    s_tz_changed = 0;
    /* Re-apply on calling task — newlib tzset() is per-process but
     * calling it again on the LVGL task ensures localtime_r picks it up */
    const char *tz = getenv("TZ");
    if (tz)
    {
        setenv("TZ", tz, 1);
        tzset();
    }
}