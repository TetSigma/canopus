#pragma once

#include "service.h"

typedef enum
{
    TEMPLATE_SERVICE_EVT_DONE,
} template_service_evt_t;

typedef struct
{
    template_service_evt_t event;
    service_session_t session;
} template_service_notify_t;

typedef void (*template_service_handler_t)(const template_service_notify_t *n,
                                           void *user_ctx);

service_session_t template_service_open(void);
void template_service_close(service_session_t session);

void template_service_set_handler(template_service_handler_t handler,
                                  void *user_ctx);

/** Example async op — fires TEMPLATE_SERVICE_EVT_DONE when finished. */
void template_service_ping(service_session_t session);

bool template_service_is_session_active(service_session_t session);
