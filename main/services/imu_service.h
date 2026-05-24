#pragma once

/**
 * imu_service.h — QMI8658 unified IMU service
 *
 * Single service for accelerometer + gyroscope + step counter.
 * Both sensors are on the same chip — splitting them would cause
 * I2C conflicts. Read everything from here.
 */

#include "esp_err.h"
#include "driver/i2c_master.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* ── Data types ──────────────────────────────────────────────────── */

    typedef struct
    {
        float x, y, z; /* in g (1g = 9.81 m/s²) */
    } imu_accel_t;

    typedef struct
    {
        float x, y, z; /* in degrees/second */
    } imu_gyro_t;

    typedef struct
    {
        imu_accel_t accel;
        imu_gyro_t gyro;
        uint32_t steps;
        uint32_t timestamp_ms;
    } imu_data_t;

    /** Called from imu_task when significant motion is detected */
    typedef void (*imu_motion_cb_t)(void);

    /* ── Init ────────────────────────────────────────────────────────── */

    /**
     * Init QMI8658 and start background task.
     * @param i2c_bus    shared I2C bus
     * @param motion_cb  called on significant motion (can be NULL)
     */
    esp_err_t imu_service_init(i2c_master_bus_handle_t i2c_bus,
                               imu_motion_cb_t motion_cb);

    /* ── Accel ───────────────────────────────────────────────────────── */

    /** Get latest accelerometer reading */
    imu_accel_t imu_get_accel(void);

    /* ── Gyro ────────────────────────────────────────────────────────── */

    /** Get latest gyroscope reading */
    imu_gyro_t imu_get_gyro(void);

    /* ── Combined ────────────────────────────────────────────────────── */

    /** Get latest accel + gyro + steps in one call */
    imu_data_t imu_get_data(void);

    /* ── Steps ───────────────────────────────────────────────────────── */

    uint32_t imu_get_steps(void);
    void imu_reset_steps(void);

    /* ── Status ──────────────────────────────────────────────────────── */

    bool imu_ready(void);

#ifdef __cplusplus
}
#endif