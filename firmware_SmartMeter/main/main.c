#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "active_power.h"
#include "ade9153a_init.h"
#include "ade9153a_spi.h"
#include "apparent_power.h"
#include "conversion_constants.h"
#include "current.h"
#include "fault_handler.h"
#include "logger.h"
#include "meter_config.h"
#include "meter_data.h"
#include "power_factor.h"
#include "reactive_power.h"
#include "task_manager.h"
#include "temperature.h"
#include "voltage.h"
#include "esp_err.h"
#include "esp_system.h"

static const char *TAG = "main";

/**
 * @brief Punto de entrada principal del firmware.
 * @param void Sin parametros.
 * @return void
 */
void app_main(void)
{
    logger_init();
    LOG_INFO(TAG, "firmware_smartmeter starting - Sprint 4 (ADE9153A + SIM7080G + UI)");
    const bool sim_focus_mode = (METER_SIM7080G_FOCUS_MODE != 0);
    if (sim_focus_mode) {
        LOG_WARN(TAG, "SIM focus mode enabled - skipping ADE/metrology init");
    }

    ESP_ERROR_CHECK(fault_handler_init());
    ESP_ERROR_CHECK(meter_data_init());

    bool ade_ready = false;
    if (!sim_focus_mode) {
        const esp_err_t spi_err = ade9153a_spi_init();
        if (spi_err != ESP_OK) {
            LOG_ERROR(TAG, "ADE SPI init failed: %s", esp_err_to_name(spi_err));
            fault_set(FAULT_INIT, "ade9153a_spi_init failed");
        } else {
            for (int attempt = 1; attempt <= 3; ++attempt) {
                LOG_INFO(TAG, "ADE9153A init attempt %d/3", attempt);
                const esp_err_t init_err = ade9153a_init();
                if (init_err != ESP_OK) {
                    LOG_WARN(TAG, "ade9153a_init fallo: %s", esp_err_to_name(init_err));
                } else if (ade9153a_is_ready()) {
                    ade_ready = true;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(300));
            }
        }

        if (!ade_ready) {
            LOG_ERROR(TAG, "ADE9153A not responding - continuing in degraded mode");
            fault_set(FAULT_INIT, "ADE boot degraded mode");
        } else {
            LOG_INFO(TAG, "ADE9153A detected OK");
        }
    }

    if (!sim_focus_mode) {
        ESP_ERROR_CHECK(voltage_init(TARGET_AVCC));
        ESP_ERROR_CHECK(current_init(TARGET_AICC, TARGET_AICC));
        ESP_ERROR_CHECK(active_power_init(TARGET_WCC, TARGET_WHCC));
        ESP_ERROR_CHECK(reactive_power_init(TARGET_WCC, TARGET_WHCC));
        ESP_ERROR_CHECK(apparent_power_init(TARGET_WCC, TARGET_WHCC));
        ESP_ERROR_CHECK(power_factor_init());
        ESP_ERROR_CHECK(temperature_init());
    }

    ESP_ERROR_CHECK(task_manager_init());
    ESP_ERROR_CHECK(task_manager_start());

    LOG_INFO(TAG, "all tasks started - scheduler in control");
}
