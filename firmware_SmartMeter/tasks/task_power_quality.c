#include "task_power_quality.h"

#include <stdint.h>
#include <stdio.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "meter_config.h"
#include "meter_data.h"
#include "pq_monitor.h"
#include "rtos_app_config.h"
#include "sag_swell.h"
#include "system_health.h"
#include "task_manager.h"
#include "task_monitor.h"
#include "logger.h"

static const char *TAG = "task_power_quality";
static TaskHandle_t s_task_handle;

/* Registro local de eventos PQ en events_v1.jsonl (severidad INFO: no pasan
 * por /alerta, cuyo envio en vivo no cambia). Antes la SD solo guardaba la
 * columna pq del historico, sin inicio ni fin del evento. Tras un fin, un
 * nuevo inicio se registra solo despues de PQ_EVENT_HOLDOFF_MS para que un
 * valor que oscila en el umbral no inunde el registro. */
#define PQ_EVENT_HOLDOFF_MS 5000LL

typedef struct {
    uint32_t flag;
    system_event_code_t code;
    const char *name;
} pq_event_def_t;

static const pq_event_def_t k_pq_events[] = {
    {PQ_FLAG_SAG,      SYSTEM_EVENT_PQ_SAG,      "SAG"},
    {PQ_FLAG_SWELL,    SYSTEM_EVENT_PQ_SWELL,    "SWELL"},
    {PQ_FLAG_FREQ_OOR, SYSTEM_EVENT_PQ_FREQ_OOR, "FREQ"},
    {PQ_FLAG_OVERTEMP, SYSTEM_EVENT_OVERTEMP,    "OVERTEMP"},
};
#define PQ_EVENT_COUNT (sizeof(k_pq_events) / sizeof(k_pq_events[0]))

static void pq_event_detail(const pq_event_def_t *def, const MeterData_t *snap,
                            bool start, char *out, size_t out_len)
{
    const char *edge = start ? "start" : "end";
    if (def->flag == PQ_FLAG_FREQ_OOR) {
        (void)snprintf(out, out_len, "%s %s f=%.2f Hz V=%.1f V", def->name, edge,
                       (double)snap->frequency, (double)snap->vrms);
    } else if (def->flag == PQ_FLAG_OVERTEMP) {
        (void)snprintf(out, out_len, "%s %s T=%.1f C", def->name, edge,
                       (double)snap->temperature);
    } else {
        (void)snprintf(out, out_len, "%s %s V=%.2f V", def->name, edge,
                       (double)snap->vrms);
    }
}

static void pq_log_events(uint32_t flags, const MeterData_t *snap,
                          uint32_t *reported, int64_t *last_end_ms)
{
    const int64_t now_ms = esp_timer_get_time() / 1000LL;
    for (size_t i = 0U; i < PQ_EVENT_COUNT; ++i) {
        const pq_event_def_t *def = &k_pq_events[i];
        const bool active = (flags & def->flag) != 0U;
        const bool was_reported = (*reported & def->flag) != 0U;
        bool start;
        if (active && !was_reported &&
            ((now_ms - last_end_ms[i]) >= PQ_EVENT_HOLDOFF_MS)) {
            start = true;
            *reported |= def->flag;
        } else if (!active && was_reported) {
            start = false;
            *reported &= ~def->flag;
            last_end_ms[i] = now_ms;
        } else {
            continue;
        }
        char detail[SYSTEM_EVENT_DETAIL_LEN];
        pq_event_detail(def, snap, start, detail, sizeof(detail));
        LOG_INFO(TAG, "evento PQ: %s", detail);
        (void)system_health_report_occurrence(SYSTEM_MODULE_PQ, def->code,
                                              SYSTEM_SEVERITY_INFO, ESP_OK, detail);
    }
}

static void task_power_quality(void *pvParameters)
{
    (void)pvParameters;

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period_ticks = pdMS_TO_TICKS(METER_PQ_MONITOR_PERIOD_MS);
    uint32_t last_flags = pq_monitor_get_flags();
    uint32_t reported_flags = 0U;
    int64_t last_end_ms[PQ_EVENT_COUNT];
    for (size_t i = 0U; i < PQ_EVENT_COUNT; ++i) {
        last_end_ms[i] = -PQ_EVENT_HOLDOFF_MS;
    }

    for (;;) {
        vTaskDelayUntil(&last_wake, period_ticks);
        task_monitor_heartbeat(TASK_MON_POWER_QUALITY);

        const MeterData_t snap = meter_data_get_snapshot();
        const uint32_t flags = pq_monitor_process_snapshot(&snap);
        pq_log_events(flags, &snap, &reported_flags, last_end_ms);

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

