#pragma once

#include "service.h"

#define WIFI_SERVICE_MAX_AP 10

/** AP entry for UI (SSID, signal, auth). */
typedef struct
{
    char ssid[33];
    int8_t rssi;
    uint8_t authmode;
    uint8_t channel;
    uint8_t bssid[6];
} wifi_ap_info_t;

typedef enum
{
    WIFI_SERVICE_EVT_SCAN_DONE,
    WIFI_SERVICE_EVT_CONNECT_DONE,
} wifi_service_evt_t;

typedef struct
{
    wifi_service_evt_t event;
    service_session_t session;
    union
    {
        struct
        {
            int count;
            wifi_ap_info_t aps[WIFI_SERVICE_MAX_AP];
        } scan;
        struct
        {
            bool ok;
            char message[48];
        } connect;
    } u;
} wifi_service_notify_t;

typedef void (*wifi_service_handler_t)(const wifi_service_notify_t *n,
                                       void *user_ctx);

service_session_t wifi_service_open(void);
void wifi_service_close(service_session_t session);

void wifi_service_set_handler(wifi_service_handler_t handler, void *user_ctx);

void wifi_service_scan(service_session_t session);
void wifi_service_select_ap(const char *ssid, const wifi_ap_info_t *ap);
void wifi_service_connect(service_session_t session, const char *ssid,
                          const char *pass);

bool wifi_service_is_session_active(service_session_t session);
