#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /** Start HTTP dashboard on port 80. Call after WiFi gets IP. */
    esp_err_t imu_dashboard_start(void);

    /** Stop the dashboard server. */
    esp_err_t imu_dashboard_stop(void);

#ifdef __cplusplus
}
#endif