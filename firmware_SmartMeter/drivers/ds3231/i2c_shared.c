#include "i2c_shared.h"

#include <stdbool.h>

#include "board_config.h"
#include "driver/i2c.h"
#include "freertos/semphr.h"
#include "logger.h"

static const char *TAG = "i2c_shared";
static StaticSemaphore_t s_mutex_buffer;
static SemaphoreHandle_t s_mutex;
static bool s_ready;

esp_err_t i2c_shared_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buffer);
        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_ready) {
        (void)xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    const i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = OLED_SDA,
        .scl_io_num = OLED_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = OLED_I2C_CLK_HZ,
        .clk_flags = 0,
    };
    esp_err_t err = i2c_param_config(OLED_I2C_PORT, &cfg);
    if (err == ESP_OK) {
        err = i2c_driver_install(OLED_I2C_PORT, cfg.mode, 0, 0, 0);
        if ((err == ESP_ERR_INVALID_STATE) || (err == ESP_FAIL)) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        s_ready = true;
        LOG_INFO(TAG, "I2C compartido listo SDA=%d SCL=%d @%lu Hz",
                 OLED_SDA, OLED_SCL, (unsigned long)OLED_I2C_CLK_HZ);
    }
    (void)xSemaphoreGive(s_mutex);
    return err;
}

esp_err_t i2c_shared_lock(TickType_t wait_ticks)
{
    const esp_err_t err = i2c_shared_init();
    if (err != ESP_OK) {
        return err;
    }
    return (xSemaphoreTake(s_mutex, wait_ticks) == pdTRUE)
        ? ESP_OK : ESP_ERR_TIMEOUT;
}

void i2c_shared_unlock(void)
{
    if (s_mutex != NULL) {
        (void)xSemaphoreGive(s_mutex);
    }
}
