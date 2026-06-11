#include "task_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger.h"
#include "meter_config.h"
#include "rtos_app_config.h"
#include "task_calibration.h"
#include "task_communication.h"
#include "task_measurement.h"
#include "task_monitor.h"
#include "task_power_quality.h"
#include "task_ui.h"

static const char *TAG = "task_manager";

QueueHandle_t q_meter_events;
QueueHandle_t q_calib_request;
QueueHandle_t q_pq_alerts;

/**
 * @brief Crea colas del sistema.
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
 * Arranca las tareas del sistema en modo "best effort": el fallo al crear
 * una tarea se registra y se sigue con las demas, de modo que la caida de
 * un subsistema (p.ej. UI sin OLED conectado) no impida medir ni publicar.
 * Devuelve ESP_FAIL solo si ninguna tarea pudo arrancar.
 */
esp_err_t task_manager_start(void)
{
    (void)task_monitor_init();

    unsigned started = 0U;

    if (METER_SIM7080G_FOCUS_MODE != 0) {
        if (task_communication_start() == ESP_OK) {
            task_monitor_register(TASK_MON_COMMUNICATION, "comm",
                                  METER_TASK_HB_TIMEOUT_COMM_MS);
            ++started;
        } else {
            LOG_ERROR(TAG, "task_communication start failed");
        }
        if (task_ui_start() == ESP_OK) {
            task_monitor_register(TASK_MON_UI, "ui", METER_TASK_HB_TIMEOUT_UI_MS);
            ++started;
        } else {
            LOG_ERROR(TAG, "task_ui start failed");
        }
        LOG_WARN(TAG, "SIM focus mode: measurement/PQ/calibration tasks skipped");
    } else {
        const BaseType_t rc = xTaskCreatePinnedToCore(
            task_measurement,
            "task_measurement",
            TASK_STACK_MEASUREMENT,
            NULL,
            TASK_PRIO_MEASUREMENT,
            NULL,
            0
        );
        if (rc == pdPASS) {
            task_monitor_register(TASK_MON_MEASUREMENT, "measurement",
                                  METER_TASK_HB_TIMEOUT_MEASUREMENT_MS);
            ++started;
            LOG_INFO(TAG, "task_measurement started on core 0");
        } else {
            LOG_ERROR(TAG, "task_measurement create failed");
        }

        if (task_power_quality_start() == ESP_OK) {
            task_monitor_register(TASK_MON_POWER_QUALITY, "power_quality",
                                  METER_TASK_HB_TIMEOUT_PQ_MS);
            ++started;
        } else {
            LOG_ERROR(TAG, "task_power_quality start failed");
        }
        // Calibracion queda fuera del monitor: es event-driven y pasa la
        // mayor parte del tiempo bloqueada en su cola, sin latido que dar.
        if (task_calibration_start() != ESP_OK) {
            LOG_ERROR(TAG, "task_calibration start failed");
        } else {
            ++started;
        }
        if (task_communication_start() == ESP_OK) {
            task_monitor_register(TASK_MON_COMMUNICATION, "comm",
                                  METER_TASK_HB_TIMEOUT_COMM_MS);
            ++started;
        } else {
            LOG_ERROR(TAG, "task_communication start failed");
        }
        if (task_ui_start() == ESP_OK) {
            task_monitor_register(TASK_MON_UI, "ui", METER_TASK_HB_TIMEOUT_UI_MS);
            ++started;
        } else {
            LOG_ERROR(TAG, "task_ui start failed");
        }
    }

    if (started == 0U) {
        LOG_ERROR(TAG, "no task could be started");
        return ESP_FAIL;
    }

    if (task_monitor_start() != ESP_OK) {
        LOG_WARN(TAG, "supervisor not started - tasks run unmonitored");
    }
    return ESP_OK;
}

/**
 * @brief Publica una solicitud de calibracion.
 * @param request_type Tipo de solicitud.
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
