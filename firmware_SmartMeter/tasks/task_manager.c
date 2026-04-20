#include "task_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger.h"
#include "meter_config.h"
#include "rtos_app_config.h"
#include "task_calibration.h"
#include "task_communication.h"
#include "task_measurement.h"
#include "task_power_quality.h"
#include "task_ui.h"

static const char *TAG = "task_manager";

QueueHandle_t q_meter_events;
QueueHandle_t q_calib_request;
QueueHandle_t q_pq_alerts;

/**
 * @brief Crea colas del sistema.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t task_manager_init(void)
{
    q_meter_events = xQueueCreate(QUEUE_METER_EVENTS_LEN, sizeof(MeterEvent_t));
    q_calib_request = xQueueCreate(QUEUE_CALIB_REQUEST_LEN, sizeof(CalibRequest_t));
    q_pq_alerts = xQueueCreate(QUEUE_PQ_ALERTS_LEN, sizeof(uint32_t));

    if ((q_meter_events == NULL) || (q_calib_request == NULL) || (q_pq_alerts == NULL)) {
        LOG_ERROR(TAG, "queue creation failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Arranca tareas activas del sprint.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t task_manager_start(void)
{
    if (METER_SIM7080G_FOCUS_MODE != 0) {
        if (task_communication_start() != ESP_OK) {
            LOG_ERROR(TAG, "task_communication start failed");
            return ESP_FAIL;
        }
        if (task_ui_start() != ESP_OK) {
            LOG_ERROR(TAG, "task_ui start failed");
            return ESP_FAIL;
        }
        LOG_WARN(TAG, "SIM focus mode: measurement/PQ/calibration tasks skipped");
        return ESP_OK;
    }

    BaseType_t rc = xTaskCreatePinnedToCore(
        task_measurement,
        "task_measurement",
        TASK_STACK_MEASUREMENT,
        NULL,
        TASK_PRIO_MEASUREMENT,
        NULL,
        0
    );
    if (rc != pdPASS) {
        LOG_ERROR(TAG, "task_measurement create failed");
        return ESP_FAIL;
    }

    LOG_INFO(TAG, "task_measurement started on core 0");

    if (task_power_quality_start() != ESP_OK) {
        LOG_ERROR(TAG, "task_power_quality start failed");
        return ESP_FAIL;
    }
    if (task_calibration_start() != ESP_OK) {
        LOG_ERROR(TAG, "task_calibration start failed");
        return ESP_FAIL;
    }
    if (task_communication_start() != ESP_OK) {
        LOG_ERROR(TAG, "task_communication start failed");
        return ESP_FAIL;
    }
    if (task_ui_start() != ESP_OK) {
        LOG_ERROR(TAG, "task_ui start failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief Publica una solicitud de calibracion.
 * @param request_type Tipo de solicitud.
 * @return void
 */
void task_manager_request_calibration(uint8_t request_type)
{
    if (q_calib_request == NULL) {
        LOG_WARN(TAG, "q_calib_request not initialized");
        return;
    }

    CalibRequest_t req = {.request_type = request_type};
    if (xQueueSend(q_calib_request, &req, 0) != pdTRUE) {
        LOG_WARN(TAG, "q_calib_request full - request discarded");
    }
}
