#pragma once

#include "screen.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define APP_MANAGER_TICK_MS 100
#define APP_MANAGER_MAX_SCREENS 16
#define APP_MANAGER_ANIM_MS 250

    esp_err_t app_manager_init(void);
    esp_err_t app_manager_register(const screen_t *screen);
    esp_err_t app_manager_register_overlay(const screen_t *screen);
    esp_err_t app_manager_switch_to(const char *name);
    esp_err_t app_manager_go_back(void);
    const char *app_manager_active_name(void);

#ifdef __cplusplus
}
#endif