#pragma once

/**
 * clock_service.h
 *
 * Fetches time from NTP, sets the system clock, persists the last
 * known time to NVS so reboots without WiFi still show a sane time.
 *
 * Usage
 * ─────
 *   clock_service_init();          // call once at boot, before LVGL
 *   clock_service_sync();          // call after WiFi connects
 *
 *   // In watchface tick():
 *   time_t now = clock_service_now();
 *   struct tm t;
 *   localtime_r(&now, &t);
 */

#include <time.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Init — loads last saved time from NVS and sets the system clock.
     * Call once at boot. Safe to call before WiFi is up.
     */
    void clock_service_init(void);

    /**
     * Sync — spawns a background task that fetches time from NTP.
     * Call after WiFi gets an IP. Non-blocking, does not crash if
     * the server is unreachable — falls back to NVS time silently.
     */
    void clock_service_sync(void);

    /**
     * Returns current time. After clock_service_init() this is either:
     *   - NTP-synced time (best)
     *   - Last saved NVS time + elapsed ticks (good enough)
     *   - Epoch 0 (if never synced and no NVS — shows 00:00 Jan 1 1970)
     */
    time_t clock_service_now(void);

    /**
     * Returns true if NTP sync has succeeded at least once this session.
     */
    bool clock_service_is_synced(void);

    /** Force all screens to refresh time on next tick (call after TZ change) */
    void clock_service_force_tick_refresh(void);

    /** Returns true once after a TZ change — resets to false after being read */
    bool clock_service_tz_changed(void);

#ifdef __cplusplus
}
#endif