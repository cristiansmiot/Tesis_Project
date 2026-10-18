#include "system_event_dispatcher.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "data_serializer.h"
#include "logger.h"
#include "meter_config.h"
#include "mqtt_topics.h"
#include "rtos_app_config.h"
#include "storage_service.h"
#include "system_health.h"

#define SYSTEM_ALERT_QUEUE_LEN      32U

static const char *TAG = "event_dispatch";
static TaskHandle_t s_task_handle;
static StaticQueue_t s_alert_queue_buffer;
static uint8_t s_alert_queue_storage[SYSTEM_ALERT_QUEUE_LEN * sizeof(system_event_t)];
static QueueHandle_t s_alert_queue;

static void sanitize_detail(const char *src, char *dst, size_t dst_len)
{
    if ((dst == NULL) || (dst_len == 0U)) {
        return;
    }
    size_t out = 0U;
    if (src != NULL) {
        for (size_t i = 0U; (src[i] != '\0') && (out + 1U < dst_len); ++i) {
            const char c = src[i];
            if ((c == '"') || (c == '\\') || (c == '\n') || (c == '\r')) {
                dst[out++] = ' ';
            } else {
                dst[out++] = c;
            }
        }
    }
    dst[out] = '\0';
}

static void format_timestamp(int64_t epoch_utc_s,
                             bool valid,
                             char *utc,
                             size_t utc_len,
                             char *local,
                             size_t local_len)
{
    if (!valid || !system_time_epoch_is_valid(epoch_utc_s)) {
        if (utc_len > 0U) {
            utc[0] = '\0';
        }
        if (local_len > 0U) {
            local[0] = '\0';
        }
        return;
    }

    const time_t utc_epoch = (time_t)epoch_utc_s;
    const time_t local_epoch = utc_epoch +
        ((time_t)SYSTEM_TIMEZONE_OFFSET_MIN * (time_t)60);
    struct tm utc_tm = {0};
    struct tm local_tm = {0};
    (void)gmtime_r(&utc_epoch, &utc_tm);
    (void)gmtime_r(&local_epoch, &local_tm);
    (void)strftime(utc, utc_len, "%Y-%m-%dT%H:%M:%SZ", &utc_tm);
    (void)strftime(local, local_len, "%Y-%m-%dT%H:%M:%S-05:00", &local_tm);
}

static void persist_event(const system_event_t *event)
{
    if (event == NULL) {
        return;
    }

    char utc[32];
    char local[40];
    char detail[SYSTEM_EVENT_DETAIL_LEN];
    char line[512];
    format_timestamp(event->timestamp_utc_s, event->time_valid,
                     utc, sizeof(utc), local, sizeof(local));
    sanitize_detail(event->detail, detail, sizeof(detail));

    const int len = snprintf(
        line, sizeof(line),
        "{\"v\":%u,\"device\":\"%s\",\"boot\":%lu,\"seq\":%lu,"
        "\"ts_utc\":%lld,\"utc\":\"%s\",\"local\":\"%s\","
        "\"time_valid\":%s,\"uptime_ms\":%lld,\"type\":\"event\","
        "\"severity\":\"%s\",\"module\":\"%s\",\"code\":\"%s\","
        "\"transition\":\"%s\",\"state\":\"%s\",\"error\":%ld,"
        "\"count\":%lu,\"detail\":\"%s\"}",
        (unsigned)event->version,
        mqtt_topics_device_id(),
        (unsigned long)event->boot_id,
        (unsigned long)event->sequence,
        (long long)event->timestamp_utc_s,
        utc,
        local,
        event->time_valid ? "true" : "false",
        (long long)(event->uptime_us / 1000LL),
        system_severity_name(event->severity),
        system_module_name(event->module),
        system_event_code_name(event->code),
        system_event_transition_name(event->transition),
        system_state_name(event->resulting_state),
        (long)event->error,
        (unsigned long)event->occurrence_count,
        detail);
    if ((len > 0) && ((size_t)len < sizeof(line))) {
        if (storage_service_submit_event(line) != ESP_OK) {
            LOG_WARN(TAG, "no se pudo persistir evento seq=%lu",
                     (unsigned long)event->sequence);
        }
    }
}

static bool persist_alert(const system_event_t *event)
{
    if (event == NULL) {
        return false;
    }
    char payload[768];
    if (data_serializer_build_senml_system_event(event, payload,
                                                 sizeof(payload)) != ESP_OK) {
        return false;
    }
    return storage_service_submit_alert(payload) == ESP_OK;
}

static void enqueue_alert(const system_event_t *event)
{
    if ((event == NULL) || (event->severity < SYSTEM_SEVERITY_WARNING)) {
        return;
    }
    if (persist_alert(event)) {
        return;
    }
    if (xQueueSend(s_alert_queue, event, 0) != pdTRUE) {
        system_event_t oldest;
        if (xQueueReceive(s_alert_queue, &oldest, 0) == pdTRUE) {
            (void)xQueueSend(s_alert_queue, event, 0);
        }
        LOG_WARN(TAG, "cola MQTT de alertas llena; se reemplazo la mas antigua");
    }
}

static void dispatch_event(const system_event_t *event)
{
    if (event->severity == SYSTEM_SEVERITY_CRITICAL) {
        LOG_ERROR(TAG, "%s/%s %s state=%s err=%s count=%lu detail=%s",
                  system_module_name(event->module),
                  system_event_code_name(event->code),
                  system_event_transition_name(event->transition),
                  system_state_name(event->resulting_state),
                  esp_err_to_name(event->error),
                  (unsigned long)event->occurrence_count,
                  event->detail);
    } else if (event->severity == SYSTEM_SEVERITY_WARNING) {
        LOG_WARN(TAG, "%s/%s %s state=%s err=%s count=%lu detail=%s",
                 system_module_name(event->module),
                 system_event_code_name(event->code),
                 system_event_transition_name(event->transition),
                 system_state_name(event->resulting_state),
                 esp_err_to_name(event->error),
                 (unsigned long)event->occurrence_count,
                 event->detail);
    } else {
        LOG_INFO(TAG, "%s/%s %s state=%s detail=%s",
                 system_module_name(event->module),
                 system_event_code_name(event->code),
                 system_event_transition_name(event->transition),
                 system_state_name(event->resulting_state),
                 event->detail);
    }

    persist_event(event);
    enqueue_alert(event);
}

static void task_system_events(void *arg)
{
    (void)arg;
    system_event_t event;
    for (;;) {
        if (system_event_receive(&event, portMAX_DELAY) == ESP_OK) {
            dispatch_event(&event);
        }
    }
}

esp_err_t system_event_dispatcher_start(void)
{
    if (s_task_handle != NULL) {
        return ESP_OK;
    }
    if (!system_health_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_alert_queue == NULL) {
        s_alert_queue = xQueueCreateStatic(SYSTEM_ALERT_QUEUE_LEN,
                                           sizeof(system_event_t),
                                           s_alert_queue_storage,
                                           &s_alert_queue_buffer);
    }
    if (s_alert_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    (void)data_serializer_init();

    const BaseType_t rc = xTaskCreatePinnedToCore(
        task_system_events,
        "task_events",
        TASK_STACK_SYSTEM_EVENTS,
        NULL,
        TASK_PRIO_SYSTEM_EVENTS,
        &s_task_handle,
        tskNO_AFFINITY);
    if (rc != pdPASS) {
        s_task_handle = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t system_alert_peek(system_event_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_alert_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return (xQueuePeek(s_alert_queue, out, 0) == pdTRUE)
               ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t system_alert_discard(void)
{
    if (s_alert_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    system_event_t discarded;
    return (xQueueReceive(s_alert_queue, &discarded, 0) == pdTRUE)
               ? ESP_OK : ESP_ERR_NOT_FOUND;
}

uint32_t system_alert_pending_count(void)
{
    return (s_alert_queue != NULL) ? (uint32_t)uxQueueMessagesWaiting(s_alert_queue) : 0U;
}
