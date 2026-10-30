#include "spi_shared_bus.h"

#include <stdbool.h>
#include "board_config.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "logger.h"

static const char *TAG = "spi_shared";
static bool s_initialized;
static StaticSemaphore_t s_init_mutex_buffer;
static SemaphoreHandle_t s_init_mutex;

esp_err_t spi_shared_bus_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    if (s_init_mutex == NULL) {
        s_init_mutex = xSemaphoreCreateMutexStatic(&s_init_mutex_buffer);
        if (s_init_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (xSemaphoreTake(s_init_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_initialized) {
        (void)xSemaphoreGive(s_init_mutex);
        return ESP_OK;
    }

    const spi_bus_config_t bus_cfg = {
        .mosi_io_num = ADE9153A_PIN_MOSI,
        .miso_io_num = ADE9153A_PIN_MISO,
        .sclk_io_num = ADE9153A_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    const esp_err_t err = spi_bus_initialize(ADE9153A_SPI_HOST,
                                              &bus_cfg,
                                              SPI_DMA_CH_AUTO);
    if (err == ESP_OK) {
        s_initialized = true;
        LOG_INFO(TAG, "SPI2 compartido inicializado (ADE CS=%d, SD CS=%d)",
                 ADE9153A_PIN_CS, SD_PIN_CS);
    }
    (void)xSemaphoreGive(s_init_mutex);
    return err;
}
