#pragma once

/**
 * service.h — shared conventions for background services (no UI).
 *
 * Services own hardware, networking, persistence, and async work.
 * Screens call service APIs and update LVGL from notify callbacks
 * (use lvgl_port_lock in the handler — callbacks run on worker tasks).
 *
 * FILE LAYOUT (mirror screens/):
 *   service.h              — this file
 *   services/<name>_service.c
 *   services/<name>_service.h   — public API for that service (optional)
 *
 * NAMING RULE: prefix static functions with the service name.
 *   wifi_service_scan(), wifi_svc_notify() — NOT scan(), notify()
 *
 * SESSIONS: long-running UI flows use open/close and pass session id to
 * async ops; notifications include the same id so the screen can ignore
 * stale work after navigate away.
 */

#include <stdint.h>
#include <stdbool.h>

/** Returned by service close or before first open — never a valid session. */
#define SERVICE_SESSION_INVALID 0u

/** Opaque session handle (each service increments its own counter). */
typedef uint32_t service_session_t;

#ifdef __cplusplus
extern "C"
{
#endif

    /* Per-service notify types live in services/<name>_service.h */

#ifdef __cplusplus
}
#endif
