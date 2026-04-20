#include "meter_data.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "rtos_app_config.h"
#include "fault_handler.h"
#include "logger.h"

static const char *TAG = "meter_data";

static MeterData_t s_meter_data;
static MeterData_t s_last_snapshot;
static SemaphoreHandle_t s_mutex;

/**
 * @brief Inicializa el buffer compartido de medicion.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t meter_data_init(void)
{
    (void)memset(&s_meter_data, 0, sizeof(s_meter_data));
    (void)memset(&s_last_snapshot, 0, sizeof(s_last_snapshot));

    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
    if (s_mutex == NULL) {
        fault_set(FAULT_MUTEX, "meter_data_init: mutex create failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Actualiza snapshot compartido con proteccion de mutex.
 * @param new_data Datos nuevos.
 * @return ESP_OK en caso de exito.
 */
esp_err_t meter_data_update(const MeterData_t *new_data)
{
    if (new_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(TIMEOUT_MUTEX_MS)) != pdTRUE) {
        LOG_WARN(TAG, "meter_data_update timeout");
        fault_set(FAULT_MUTEX, "meter_data_update timeout");
        return ESP_ERR_TIMEOUT;
    }

    s_meter_data = *new_data;
    s_last_snapshot = s_meter_data;
    (void)xSemaphoreGive(s_mutex);
    return ESP_OK;
}

/**
 * @brief Lee snapshot compartido de forma atomica.
 * @param void Sin parametros.
 * @return Copia de datos de medicion.
 */
MeterData_t meter_data_get_snapshot(void)
{
    if (s_mutex == NULL) {
        return s_last_snapshot;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(TIMEOUT_MUTEX_MS)) != pdTRUE) {
        LOG_WARN(TAG, "meter_data_get_snapshot timeout; using fallback");
        fault_set(FAULT_MUTEX, "meter_data_get_snapshot timeout");
        return s_last_snapshot;
    }

    s_last_snapshot = s_meter_data;
    (void)xSemaphoreGive(s_mutex);
    return s_last_snapshot;
}
