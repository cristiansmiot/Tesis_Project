#include "task_power_quality.h"

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "meter_config.h"
#include "meter_data.h"
#include "pq_monitor.h"
#include "rtos_app_config.h"
#include "sag_swell.h"
#include "task_manager.h"
#include "logger.h"

static const char *TAG = "task_power_quality";
static TaskHandle_t s_task_handle;

static void task_power_quality(void *pvParameters)
{
    (void)pvParameters;

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period_ticks = pdMS_TO_TICKS(METER_PQ_MONITOR_PERIOD_MS);
    uint32_t last_flags = pq_monitor_get_flags();

    for (;;) {
        vTaskDelayUntil(&last_wake, period_ticks);

        const MeterData_t snap = meter_data_get_snapshot();
        const uint32_t flags = pq_monitor_process_snapshot(&snap);

        if ((flags != last_flags) && (q_pq_alerts != NULL)) {
            const uint32_t alert = flags;
            if (xQueueSend(q_pq_alerts, &alert, 0) != pdTRUE) {
                LOG_WARN(TAG, "q_pq_alerts full - alert discarded");
            }
        }
        last_flags = flags;
    }
}

/**
 * @brief Crea y arranca task_power_quality.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t task_power_quality_start(void)
{
    if (s_task_handle != NULL) {
        return ESP_OK;
    }

    esp_err_t err = sag_swell_init();
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "sag_swell_init failed");
        return err;
    }
    err = pq_monitor_init();
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "pq_monitor_init failed");
        return err;
    }

    const BaseType_t rc = xTaskCreatePinnedToCore(
        task_power_quality,
        "task_power_quality",
        TASK_STACK_PQ,
        NULL,
        TASK_PRIO_POWER_QUALITY,
        &s_task_handle,
        1
    );
    if (rc != pdPASS) {
        LOG_ERROR(TAG, "task_power_quality create failed");
        s_task_handle = NULL;
        return ESP_FAIL;
    }

    LOG_INFO(TAG, "task_power_quality started on core 1");
    return ESP_OK;
}

