#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "active_power.h"
#include "ade9153a_init.h"
#include "ade9153a_spi.h"
#include "apparent_power.h"
#include "conversion_constants.h"
#include "current.h"
#include "fault_handler.h"
#include "hardware_test.h"
#include "logger.h"
#include "meter_config.h"
#include "meter_data.h"
#include "power_factor.h"
#include "reactive_power.h"
#include "task_manager.h"
#include "temperature.h"
#include "system_health.h"
#include "voltage.h"
#include "esp_err.h"
#include "esp_system.h"

static const char *TAG = "main";

/**
 * @brief Punto de entrada principal del firmware.
 */
void app_main(void)
{
    logger_init();

    if (hardware_test_is_standalone()) {
        hardware_test_run();
        return;
    }

    LOG_INFO(TAG, "firmware_smartmeter starting - Sprint 4 (ADE9153A + SIM7080G + UI)");
    const bool sim_focus_mode = (METER_SIM7080G_FOCUS_MODE != 0);
    if (METER_HW_TEST_PROFILE != HW_TEST_PROFILE_NONE) {
        LOG_WARN(TAG, "hardware integration profile: %s",
                 hardware_test_profile_name());
    }
    if (sim_focus_mode) {
        LOG_WARN(TAG, "SIM focus mode enabled - skipping ADE/metrology init");
    }

    ESP_ERROR_CHECK(fault_handler_init());
    ESP_ERROR_CHECK(meter_data_init());

    bool ade_ready = false;
    if (sim_focus_mode) {
        (void)system_health_set_disabled(SYSTEM_MODULE_ADE,
                                         "ADE disabled by profile");
        (void)system_health_set_disabled(SYSTEM_MODULE_PQ,
                                         "PQ disabled with metrology");
        (void)system_health_set_disabled(SYSTEM_MODULE_NVS,
                                         "energy NVS disabled with metrology");
    } else {
        (void)system_health_mark_recovering(SYSTEM_MODULE_ADE,
                                            "ADE boot initialization");
        const esp_err_t spi_err = ade9153a_spi_init();
        if (spi_err != ESP_OK) {
            LOG_ERROR(TAG, "ADE SPI init failed: %s", esp_err_to_name(spi_err));
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
            (void)system_health_report_failure(
                SYSTEM_MODULE_ADE,
                (spi_err == ESP_OK) ? SYSTEM_EVENT_INIT_FAILED
                                    : SYSTEM_EVENT_SPI_ERROR,
                SYSTEM_SEVERITY_WARNING,
                (spi_err == ESP_OK) ? ESP_FAIL : spi_err,
                "ADE boot degraded mode");
        } else {
            LOG_INFO(TAG, "ADE9153A detected OK");
            (void)system_health_report_ok(SYSTEM_MODULE_ADE,
                                          "ADE detected");
        }
    }

    if (!sim_focus_mode) {
        // Los modulos de medicion solo cachean constantes de conversion:
        // un fallo aqui es de configuracion, no de hardware. Se registra y
        // se continua para que comunicacion/UI sigan reportando el estado
        // del nodo en vez de dejarlo mudo con un abort().
        struct {
            const char *name;
            esp_err_t err;
        } init_results[] = {
            { "voltage",        voltage_init(TARGET_AVCC) },
            { "current",        current_init(TARGET_AICC, TARGET_AICC) },
            { "active_power",   active_power_init(TARGET_WCC, TARGET_WHCC) },
            { "reactive_power", reactive_power_init(TARGET_WCC, TARGET_WHCC) },
            { "apparent_power", apparent_power_init(TARGET_WCC, TARGET_WHCC) },
            { "power_factor",   power_factor_init() },
            { "temperature",    temperature_init() },
        };
        for (size_t i = 0; i < sizeof(init_results) / sizeof(init_results[0]); ++i) {
            if (init_results[i].err != ESP_OK) {
                LOG_ERROR(TAG, "%s_init fallo: %s (modo degradado)",
                          init_results[i].name, esp_err_to_name(init_results[i].err));
                fault_set(FAULT_INIT, init_results[i].name);
            }
        }
    }

    // Las colas y el arranque de tareas si son condicion de vida del nodo:
    // sin scheduler poblado no hay nada que degradar, solo reiniciar.
    ESP_ERROR_CHECK(task_manager_init());
    ESP_ERROR_CHECK(task_manager_start());

    LOG_INFO(TAG, "all tasks started - scheduler in control");
}
