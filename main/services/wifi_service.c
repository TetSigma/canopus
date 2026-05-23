/**
 * wifi_service.c — WiFi scan, connect, NVS persistence (no UI).
 */

#include "wifi_service.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi_svc";

#define NVS_NAMESPACE "wifi_cfg"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "pass"
#define NVS_KEY_AUTH "auth"
#define NVS_KEY_CH "ch"
#define NVS_KEY_BSSID "bssid"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_MAX_CONNECT_RETRY 4
#define WIFI_RETRY_DELAY_MS 2000
#define WIFI_CONNECT_PROFILES 4

static wifi_service_handler_t s_handler;
static void *s_handler_ctx;

static wifi_ap_info_t s_selected_ap;
static bool s_ap_selected;
static char s_task_ssid[32];
static char s_task_pass[64];

static int s_connect_retries;
static int s_connect_profile;
static int s_last_disconnect_reason;
static bool s_l2_connected;
static bool s_events_registered;
static bool s_wifi_global_inited;
static bool s_wifi_stack_up;
static service_session_t s_session = SERVICE_SESSION_INVALID;

static EventGroupHandle_t s_wifi_eg;

static void wifi_svc_notify(const wifi_service_notify_t *n)
{
    if (s_handler)
        s_handler(n, s_handler_ctx);
}

static bool wifi_svc_session_active(service_session_t session)
{
    return session == s_session && s_session != SERVICE_SESSION_INVALID;
}

static void nvs_save_wifi(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
        return;
    nvs_set_str(h, NVS_KEY_SSID, ssid);
    nvs_set_str(h, NVS_KEY_PASS, pass);
    nvs_set_u8(h, NVS_KEY_AUTH,
               s_ap_selected ? s_selected_ap.authmode
                             : (uint8_t)WIFI_AUTH_WPA2_PSK);
    if (s_ap_selected)
    {
        nvs_set_u8(h, NVS_KEY_CH, s_selected_ap.channel);
        nvs_set_blob(h, NVS_KEY_BSSID, s_selected_ap.bssid, 6);
    }
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "saved credentials for '%s'", ssid);
}

static void trim_password(char *pass)
{
    if (!pass)
        return;
    size_t start = 0;
    while (pass[start] == ' ' || pass[start] == '\t')
        start++;
    if (start > 0)
        memmove(pass, pass + start, strlen(pass + start) + 1);

    size_t n = strlen(pass);
    while (n > 0 && (pass[n - 1] == ' ' || pass[n - 1] == '\t'))
        pass[--n] = '\0';
}

static void copy_field(uint8_t *dst, size_t dst_len, const char *src)
{
    size_t len = strnlen(src, dst_len);
    memcpy(dst, src, len);
    if (len < dst_len)
        dst[len] = '\0';
    else if (dst_len > 0)
        dst[dst_len - 1] = '\0';
}

static void fill_sta_config(wifi_config_t *wcfg)
{
    memset(wcfg, 0, sizeof(*wcfg));
    copy_field(wcfg->sta.ssid, sizeof(wcfg->sta.ssid), s_task_ssid);
    copy_field(wcfg->sta.password, sizeof(wcfg->sta.password), s_task_pass);
    wcfg->sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    wcfg->sta.pmf_cfg.capable = false;
    wcfg->sta.pmf_cfg.required = false;
}

static void radio_reset(void)
{
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_wifi_start();
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_wifi_set_ps(WIFI_PS_NONE);
}

static bool refresh_selected_ap(void)
{
    wifi_scan_config_t scan_cfg = {
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };

    esp_wifi_scan_stop();
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(300));

    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK)
    {
        ESP_LOGW(TAG, "pre-connect scan failed");
        return s_ap_selected;
    }

    wifi_ap_record_t recs[WIFI_SERVICE_MAX_AP];
    uint16_t count = WIFI_SERVICE_MAX_AP;
    if (esp_wifi_scan_get_ap_records(&count, recs) != ESP_OK || count == 0)
    {
        ESP_LOGW(TAG, "pre-connect scan: no APs in range");
        return s_ap_selected;
    }
    ESP_LOGI(TAG, "pre-connect scan: %u AP(s) total", count);

    bool found = false;
    int best_rssi = -128;
    for (uint16_t i = 0; i < count; i++)
    {
        if (strcmp((const char *)recs[i].ssid, s_task_ssid) != 0)
            continue;
        if (recs[i].rssi > best_rssi)
        {
            best_rssi = recs[i].rssi;
            s_selected_ap.ssid[0] = '\0';
            strncpy(s_selected_ap.ssid, (const char *)recs[i].ssid,
                    sizeof(s_selected_ap.ssid) - 1);
            s_selected_ap.rssi = recs[i].rssi;
            s_selected_ap.authmode = recs[i].authmode;
            s_selected_ap.channel = recs[i].primary;
            memcpy(s_selected_ap.bssid, recs[i].bssid, 6);
            found = true;
        }
    }

    if (found)
    {
        s_ap_selected = true;
        ESP_LOGI(TAG, "refreshed '%s' auth=%d ch=%d rssi=%d",
                 s_task_ssid, (int)s_selected_ap.authmode, s_selected_ap.channel,
                 s_selected_ap.rssi);
    }
    return found;
}

static void restart_dhcp(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif)
        return;
    esp_netif_dhcpc_stop(netif);
    esp_err_t err = esp_netif_dhcpc_start(netif);
    ESP_LOGI(TAG, "DHCP restart: %s", esp_err_to_name(err));
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id,
                               void *data)
{
    (void)arg;
    if (base == WIFI_EVENT)
    {
        if (id == WIFI_EVENT_STA_CONNECTED)
        {
            s_connect_retries = 0;
            s_l2_connected = true;
            ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED");
            restart_dhcp();
            return;
        }
        if (id == WIFI_EVENT_STA_DISCONNECTED)
        {
            const wifi_event_sta_disconnected_t *disc = data;
            s_last_disconnect_reason = disc ? (int)disc->reason : -1;
            const bool was_l2 = s_l2_connected;
            s_l2_connected = false;
            ESP_LOGW(TAG, "WIFI_EVENT_STA_DISCONNECTED reason=%d retry=%d had_l2=%d",
                     s_last_disconnect_reason, s_connect_retries, was_l2);

            if (!s_wifi_eg)
                return;

            if (s_connect_retries < WIFI_MAX_CONNECT_RETRY)
            {
                s_connect_retries++;
                s_connect_profile++;
                vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS));

                if (s_last_disconnect_reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT)
                    radio_reset();

                esp_wifi_set_storage(WIFI_STORAGE_RAM);
                wifi_config_t wcfg;
                fill_sta_config(&wcfg);
                esp_wifi_set_config(WIFI_IF_STA, &wcfg);
                esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20);
                esp_wifi_connect();
            }
            else
            {
                xEventGroupSetBits(s_wifi_eg, WIFI_FAIL_BIT);
            }
            return;
        }
        if (s_wifi_eg)
            ESP_LOGD(TAG, "WIFI_EVENT id=%ld", (long)id);
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP");
        if (s_wifi_eg)
            xEventGroupSetBits(s_wifi_eg, WIFI_CONNECTED_BIT);
    }
}

static void stack_ensure_started(void)
{
    if (!s_wifi_global_inited)
    {
        ESP_ERROR_CHECK(esp_netif_init());
        esp_err_t err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
            ESP_ERROR_CHECK(err);
        esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        if (!s_events_registered)
        {
            esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                wifi_event_handler, NULL, NULL);
            esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                wifi_event_handler, NULL, NULL);
            s_events_registered = true;
        }
        s_wifi_global_inited = true;
    }

    if (s_wifi_stack_up)
        return;

    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_ERROR_CHECK(esp_wifi_start());
    s_wifi_stack_up = true;
    ESP_LOGI(TAG, "WiFi started");
}

static void stack_stop(void)
{
    if (!s_wifi_stack_up)
        return;
    esp_wifi_scan_stop();
    esp_wifi_disconnect();
    esp_wifi_stop();
    s_wifi_stack_up = false;
    ESP_LOGI(TAG, "WiFi stopped");
}

static void record_to_info(const wifi_ap_record_t *rec, wifi_ap_info_t *out)
{
    out->ssid[0] = '\0';
    strncpy(out->ssid, (const char *)rec->ssid, sizeof(out->ssid) - 1);
    out->rssi = rec->rssi;
    out->authmode = rec->authmode;
    out->channel = rec->primary;
    memcpy(out->bssid, rec->bssid, 6);
}

static void scan_task(void *arg)
{
    const service_session_t session = (service_session_t)(uintptr_t)arg;

    if (!wifi_svc_session_active(session))
        vTaskDelete(NULL);

    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));

    if (!wifi_svc_session_active(session))
        vTaskDelete(NULL);

    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    esp_wifi_scan_start(&scan_cfg, true);

    if (!wifi_svc_session_active(session))
        vTaskDelete(NULL);

    wifi_ap_record_t recs[WIFI_SERVICE_MAX_AP];
    uint16_t count = WIFI_SERVICE_MAX_AP;
    esp_wifi_scan_get_ap_records(&count, recs);
    ESP_LOGI(TAG, "Found %d networks", count);

    wifi_service_notify_t n = {
        .event = WIFI_SERVICE_EVT_SCAN_DONE,
        .session = session,
    };
    n.u.scan.count = count > WIFI_SERVICE_MAX_AP ? WIFI_SERVICE_MAX_AP : (int)count;
    for (int i = 0; i < n.u.scan.count; i++)
        record_to_info(&recs[i], &n.u.scan.aps[i]);

    wifi_svc_notify(&n);
    vTaskDelete(NULL);
}

static void connect_result_message(char *result, size_t result_len, EventBits_t bits)
{
    if (bits & WIFI_CONNECTED_BIT)
    {
        nvs_save_wifi(s_task_ssid, s_task_pass);
        snprintf(result, result_len, "Connected!");
        return;
    }
    if (bits & WIFI_FAIL_BIT)
    {
        if (s_last_disconnect_reason == WIFI_REASON_AUTH_FAIL ||
            s_last_disconnect_reason == WIFI_REASON_AUTH_EXPIRE ||
            s_last_disconnect_reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
            s_last_disconnect_reason == WIFI_REASON_HANDSHAKE_TIMEOUT)
        {
            snprintf(result, result_len, "Failed — check password");
        }
        else if (s_last_disconnect_reason == 205)
        {
            snprintf(result, result_len, "Failed — AP busy, wait");
        }
        else if (s_last_disconnect_reason == 210)
        {
            snprintf(result, result_len, "Failed — security mismatch");
        }
        else
        {
            snprintf(result, result_len, "Failed (reason %d)",
                     s_last_disconnect_reason);
        }
        return;
    }
    snprintf(result, result_len, "Timeout");
}

static void connect_task(void *arg)
{
    const service_session_t session = (service_session_t)(uintptr_t)arg;

    if (!wifi_svc_session_active(session))
        vTaskDelete(NULL);

    if (s_wifi_eg)
        vEventGroupDelete(s_wifi_eg);
    s_wifi_eg = xEventGroupCreate();

    stack_ensure_started();

    s_connect_retries = 0;
    s_connect_profile = 0;
    s_last_disconnect_reason = 0;
    s_l2_connected = false;

    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_protocol(WIFI_IF_STA,
                          WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    vTaskDelay(pdMS_TO_TICKS(300));

    wifi_config_t wcfg;
    fill_sta_config(&wcfg);

    ESP_LOGI(TAG, "connect '%s' pass_len=%d", s_task_ssid, (int)strlen(s_task_pass));
    esp_wifi_set_config(WIFI_IF_STA, &wcfg);
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(s_wifi_eg,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(45000));

    if (!wifi_svc_session_active(session))
    {
        if (s_wifi_eg)
        {
            vEventGroupDelete(s_wifi_eg);
            s_wifi_eg = NULL;
        }
        vTaskDelete(NULL);
    }

    wifi_service_notify_t n = {
        .event = WIFI_SERVICE_EVT_CONNECT_DONE,
        .session = session,
    };
    n.u.connect.ok = (bits & WIFI_CONNECTED_BIT) != 0;
    connect_result_message(n.u.connect.message, sizeof(n.u.connect.message), bits);
    wifi_svc_notify(&n);

    if (s_wifi_eg)
    {
        vEventGroupDelete(s_wifi_eg);
        s_wifi_eg = NULL;
    }
    vTaskDelete(NULL);
}

void wifi_service_set_handler(wifi_service_handler_t handler, void *user_ctx)
{
    s_handler = handler;
    s_handler_ctx = user_ctx;
}

service_session_t wifi_service_open(void)
{
    s_session++;
    stack_ensure_started();
    ESP_LOGI(TAG, "session %lu open", (unsigned long)s_session);
    return s_session;
}

void wifi_service_close(service_session_t session)
{
    (void)session;
    s_session++;
    if (s_wifi_eg)
        xEventGroupSetBits(s_wifi_eg, WIFI_FAIL_BIT);
    stack_stop();
    ESP_LOGI(TAG, "session closed");
}

void wifi_service_scan(service_session_t session)
{
    if (!wifi_svc_session_active(session))
        return;
    xTaskCreate(scan_task, "wifi_scan", 8192, (void *)(uintptr_t)session, 2, NULL);
}

void wifi_service_select_ap(const char *ssid, const wifi_ap_info_t *ap)
{
    s_ap_selected = false;
    if (ap)
    {
        s_selected_ap = *ap;
        s_ap_selected = true;
    }
    if (ssid)
    {
        copy_field((uint8_t *)s_task_ssid, sizeof(s_task_ssid), ssid);
        ESP_LOGI(TAG, "selected '%s' auth=%d ch=%d rssi=%d",
                 ssid, s_ap_selected ? (int)s_selected_ap.authmode : -1,
                 s_ap_selected ? s_selected_ap.channel : -1,
                 s_ap_selected ? s_selected_ap.rssi : 0);
    }
}

void wifi_service_connect(service_session_t session, const char *ssid,
                          const char *pass)
{
    if (!wifi_svc_session_active(session))
        return;

    if (ssid)
        copy_field((uint8_t *)s_task_ssid, sizeof(s_task_ssid), ssid);
    if (pass)
    {
        copy_field((uint8_t *)s_task_pass, sizeof(s_task_pass), pass);
        trim_password(s_task_pass);
    }
    else
    {
        s_task_pass[0] = '\0';
    }
    ESP_LOGI(TAG, "connect ssid='%s' pass='%s' len=%d",
             s_task_ssid, s_task_pass, (int)strlen(s_task_pass));
    ESP_LOGI(TAG, "pass before trim: '%s' len=%d", pass, (int)strlen(pass));
    xTaskCreate(connect_task, "wifi_conn", 8192, (void *)(uintptr_t)session, 3,
                NULL);
}

bool wifi_service_is_session_active(service_session_t session)
{
    return wifi_svc_session_active(session);
}
