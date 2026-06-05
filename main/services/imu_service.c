/**
 * imu_service.c — QMI8658 IMU driver
 *
 * Register addresses confirmed from QMI8658C datasheet Rev A + Rev 0.8
 * Accel: 0x35-0x3A, Gyro: 0x3B-0x40
 */

#include "imu_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <math.h>

static const char *TAG = "imu";

/* ── Registers ───────────────────────────────────────────────────── */
#define REG_WHO_AM_I 0x00 /* expected: 0x05 */
#define REG_REVISION_ID 0x01
#define REG_CTRL1 0x02 /* interface config */
#define REG_CTRL2 0x03 /* accel: [6:4]=range [3:0]=ODR */
#define REG_CTRL3 0x04 /* gyro:  [6:4]=range [3:0]=ODR */
#define REG_CTRL7 0x08 /* enable sensors */
#define REG_STATUSINT 0x2D
#define REG_STATUS0 0x2E /* bit1=gyro new data, bit0=accel new data */
#define REG_AX_L 0x35    /* accel X low  (0x35-0x3A) */
#define REG_GX_L 0x3B    /* gyro  X low  (0x3B-0x40) */

/* CTRL1: address auto-increment on burst read */
#define CTRL1_AUTO_INC 0x40

/* CTRL2 accel range bits [6:4] */
#define ACC_2G (0x00 << 4)
#define ACC_4G (0x01 << 4)
#define ACC_8G (0x02 << 4)
#define ACC_16G (0x03 << 4)

/* CTRL2 accel ODR bits [3:0] */
#define ACC_ODR_8000 0x00
#define ACC_ODR_4000 0x01
#define ACC_ODR_2000 0x02
#define ACC_ODR_1000 0x03
#define ACC_ODR_500 0x04
#define ACC_ODR_250 0x05
#define ACC_ODR_125 0x06

/* CTRL3 gyro range bits [6:4] */
#define GYR_16DPS (0x00 << 4)
#define GYR_32DPS (0x01 << 4)
#define GYR_64DPS (0x02 << 4)
#define GYR_128DPS (0x03 << 4)
#define GYR_256DPS (0x04 << 4)
#define GYR_512DPS (0x05 << 4)
#define GYR_1024DPS (0x06 << 4)

/* CTRL3 gyro ODR same values as accel */
#define GYR_ODR_500 0x04

/* CTRL7 enable bits */
#define CTRL7_ACC_EN (1 << 0)
#define CTRL7_GYR_EN (1 << 1)

/* Scale factors */
#define ACC_SCALE_8G (8.0f / 32768.0f)
#define GYR_SCALE_512 (512.0f / 32768.0f)

/* ── State ───────────────────────────────────────────────────────── */
static imu_accel_t s_accel = {0};
static imu_gyro_t s_gyro = {0};
static volatile uint32_t s_steps = 0;
static volatile bool s_ready = false;
static imu_motion_cb_t s_motion_cb = NULL;
static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static SemaphoreHandle_t s_i2c_mutex = NULL;

/* ── I2C ─────────────────────────────────────────────────────────── */
static bool try_open(uint8_t addr)
{
    if (s_dev)
    {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(s_bus, &cfg, &s_dev) == ESP_OK;
}

static uint8_t reg_read(uint8_t reg)
{
    uint8_t val = 0;
    if (xSemaphoreTake(s_i2c_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        i2c_master_transmit(s_dev, &reg, 1, pdMS_TO_TICKS(20));
        i2c_master_receive(s_dev, &val, 1, pdMS_TO_TICKS(20));
        xSemaphoreGive(s_i2c_mutex);
    }
    return val;
}

static void reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    if (xSemaphoreTake(s_i2c_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        i2c_master_transmit(s_dev, buf, 2, pdMS_TO_TICKS(20));
        xSemaphoreGive(s_i2c_mutex);
    }
}

static void reg_burst(uint8_t start, uint8_t *out, int len)
{
    if (xSemaphoreTake(s_i2c_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        i2c_master_transmit(s_dev, &start, 1, pdMS_TO_TICKS(20));
        i2c_master_receive(s_dev, out, len, pdMS_TO_TICKS(20));
        xSemaphoreGive(s_i2c_mutex);
    }
}

static inline int16_t to_s16(uint8_t lo, uint8_t hi)
{
    return (int16_t)((uint16_t)hi << 8 | lo);
}

/* ── IMU task ────────────────────────────────────────────────────── */
static void imu_task(void *arg)
{
    /* Try 0x6B first (SA0 low), then 0x6A */
    uint8_t addr = 0x6B;
    bool ok = try_open(addr) && (reg_read(REG_WHO_AM_I) == 0x05);
    if (!ok)
    {
        addr = 0x6A;
        ok = try_open(addr) && (reg_read(REG_WHO_AM_I) == 0x05);
    }
    if (!ok)
    {
        ESP_LOGE(TAG, "QMI8658 not found on 0x6A or 0x6B");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "QMI8658 @ 0x%02X  rev=0x%02X", addr, reg_read(REG_REVISION_ID));

    /* Soft reset */
    reg_write(0x60, 0xB0);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* CTRL1: I2C auto-increment, little-endian */
    reg_write(REG_CTRL1, CTRL1_AUTO_INC);

    /* Accel: ±8g, 500Hz */
    reg_write(REG_CTRL2, ACC_8G | ACC_ODR_500);

    /* Gyro: ±512°/s, 500Hz */
    reg_write(REG_CTRL3, GYR_512DPS | GYR_ODR_500);

    /* Enable both */
    reg_write(REG_CTRL7, CTRL7_ACC_EN | CTRL7_GYR_EN);
    vTaskDelay(pdMS_TO_TICKS(50));

    s_ready = true;
    ESP_LOGI(TAG, "QMI8658 ready — accel=±8g gyro=±512°/s");

    float ax_prev = 0, ay_prev = 0, az_prev = 0;
    uint32_t last_motion_ms = 0;

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(10)); /* 100Hz */

        uint8_t buf[12];
        reg_burst(REG_AX_L, buf, 12);

        float ax = to_s16(buf[0], buf[1]) * ACC_SCALE_8G;
        float ay = to_s16(buf[2], buf[3]) * ACC_SCALE_8G;
        float az = to_s16(buf[4], buf[5]) * ACC_SCALE_8G;
        float gx = to_s16(buf[6], buf[7]) * GYR_SCALE_512;
        float gy = to_s16(buf[8], buf[9]) * GYR_SCALE_512;
        float gz = to_s16(buf[10], buf[11]) * GYR_SCALE_512;

        s_accel = (imu_accel_t){ax, ay, az};
        s_gyro = (imu_gyro_t){gx, gy, gz};

        /* Motion detection */
        float delta = fabsf(ax - ax_prev) + fabsf(ay - ay_prev) + fabsf(az - az_prev);
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (delta > 0.3f && now - last_motion_ms > 800)
        {
            last_motion_ms = now;
            if (s_motion_cb)
                s_motion_cb();
        }
        ax_prev = ax;
        ay_prev = ay;
        az_prev = az;
    }
}

/* ── Public API ──────────────────────────────────────────────────── */
esp_err_t imu_service_init(i2c_master_bus_handle_t i2c_bus,
                           imu_motion_cb_t motion_cb)
{
    s_bus = i2c_bus;
    s_motion_cb = motion_cb;
    s_i2c_mutex = xSemaphoreCreateMutex();
    configASSERT(s_i2c_mutex);
    xTaskCreate(imu_task, "imu_task", 8192, NULL, 2, NULL);
    return ESP_OK;
}

imu_accel_t imu_get_accel(void) { return s_accel; }
imu_gyro_t imu_get_gyro(void) { return s_gyro; }

imu_data_t imu_get_data(void)
{
    return (imu_data_t){
        .accel = s_accel,
        .gyro = s_gyro,
        .steps = s_steps,
        .timestamp_ms = xTaskGetTickCount() * portTICK_PERIOD_MS,
    };
}

uint32_t imu_get_steps(void) { return s_steps; }
void imu_reset_steps(void) { s_steps = 0; }
bool imu_ready(void) { return s_ready; }