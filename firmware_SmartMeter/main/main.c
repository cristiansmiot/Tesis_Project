#include <sys/time.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "active_power.h"
#include "ade9153a_init.h"
#include "ade9153a_spi.h"
#include "apparent_power.h"
#include "conversion_constants.h"
#include "current.h"
#include "ds3231.h"
#include "fault_handler.h"
#include "logger.h"
#include "meter_config.h"
#include "meter_data.h"
#include "power_factor.h"
#include "reactive_power.h"
#include "sd_storage.h"
#include "task_manager.h"
#include "temperature.h"
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

    // --- RTC DS3231 (I2C compartido con OLED) ---
    // No bloqueante: si falla, el sistema sigue usando el timestamp del
    // backend (bt=0 en SenML) y la hora interna del ESP32 (sin respaldo
    // por bateria). El log permite diagnosticar cableado I2C.
    {
        const esp_err_t rtc_err = ds3231_init();
        if (rtc_err != ESP_OK) {
            LOG_WARN(TAG, "DS3231 init fallo: %s (continuando sin RTC)",
                     esp_err_to_name(rtc_err));
        } else {
            // Sembrar el reloj del sistema desde el RTC: asi el backlog SD
            // puede timestampear mediciones antes de que el modem registre
            // en red y sincronice via AT+CCLK. 1577836800 = 2020-01-01;
            // por debajo, el RTC perdio la bateria y su hora no sirve.
            time_t rtc_epoch = 0;
            if ((ds3231_get_epoch(&rtc_epoch) == ESP_OK) &&
                (rtc_epoch >= (time_t)METER_TIME_VALID_MIN_EPOCH)) {
                const struct timeval tv = { .tv_sec = rtc_epoch, .tv_usec = 0 };
                (void)settimeofday(&tv, NULL);
                LOG_INFO(TAG, "hora del sistema sembrada desde DS3231");
            } else {
                LOG_WARN(TAG, "DS3231 sin hora valida - esperar sync celular");
            }
        }
    }

    // --- Almacenamiento MicroSD (SPI3_HOST dedicado) ---
    // No bloqueante: si no hay tarjeta insertada o esta sin formato FAT,
    // el sistema sigue funcionando solo con MQTT (modo actual). La SD se
    // usara como buffer de respaldo cuando MQTT no este disponible.
    {
        const esp_err_t sd_err = sd_storage_init();
        if (sd_err != ESP_OK) {
            LOG_WARN(TAG, "SD init fallo: %s (continuando sin SD)",
                     esp_err_to_name(sd_err));
        } else {
            uint32_t total_mb = 0U;
            uint32_t free_mb = 0U;
            if (sd_storage_get_usage(&total_mb, &free_mb) == ESP_OK) {
                LOG_INFO(TAG, "SD: %lu/%lu MB libres",
                         (unsigned long)free_mb, (unsigned long)total_mb);
            }
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
