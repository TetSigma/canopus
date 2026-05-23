/**
 * service_template.c — copy this for each new service
 *
 * 1. cp service_template.c service_yourname.c
 * 2. cp service_template.h service_yourname.h
 * 3. Replace "template" / "tmpl" with your service name everywhere
 * 4. Add both files to main/CMakeLists.txt under services/
 * 5. Screen includes "yourname_service.h" and handles notify callbacks
 */

#include "service_template.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "svc_tmpl";

static template_service_handler_t s_handler;
static void *s_handler_ctx;
static service_session_t s_session = SERVICE_SESSION_INVALID;

static void tmpl_notify(const template_service_notify_t *n)
{
    if (s_handler)
        s_handler(n, s_handler_ctx);
}

static bool tmpl_session_active(service_session_t session)
{
    return session == s_session && s_session != SERVICE_SESSION_INVALID;
}

static void tmpl_ping_task(void *arg)
{
    const service_session_t session = (service_session_t)(uintptr_t)arg;

    vTaskDelay(pdMS_TO_TICKS(500));

    if (!tmpl_session_active(session))
        vTaskDelete(NULL);

    template_service_notify_t n = {
        .event = TEMPLATE_SERVICE_EVT_DONE,
        .session = session,
    };
    tmpl_notify(&n);
    vTaskDelete(NULL);
}

service_session_t template_service_open(void)
{
    s_session++;
    ESP_LOGI(TAG, "session %lu open", (unsigned long)s_session);
    return s_session;
}

void template_service_close(service_session_t session)
{
    (void)session;
    s_session++;
    ESP_LOGI(TAG, "session closed");
}

void template_service_set_handler(template_service_handler_t handler,
                                  void *user_ctx)
{
    s_handler = handler;
    s_handler_ctx = user_ctx;
}

void template_service_ping(service_session_t session)
{
    if (!tmpl_session_active(session))
        return;
    xTaskCreate(tmpl_ping_task, "tmpl_ping", 4096, (void *)(uintptr_t)session,
                2, NULL);
}

bool template_service_is_session_active(service_session_t session)
{
    return tmpl_session_active(session);
}
