#include "time_service.h"

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "ds3231.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "logger.h"
#include "system_health.h"

#define TIME_SYNC_QUEUE_LEN 4U
#define BOGOTA_OFFSET_SECONDS (-5 * 60 * 60)

typedef struct {
    system_time_source_t source;
    int64_t epoch_utc_s;
} time_sync_request_t;

static const char *TAG = "time_service";
static StaticSemaphore_t s_mutex_buffer;
static SemaphoreHandle_t s_mutex;
static StaticQueue_t s_sync_queue_buffer;
static uint8_t s_sync_queue_storage[TIME_SYNC_QUEUE_LEN * sizeof(time_sync_request_t)];
static QueueHandle_t s_sync_queue;
static time_service_snapshot_t s_snapshot;

static void snapshot_store(const ds3231_status_t *status,
                           float temperature_c,
                           esp_err_t error)
{
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (status != NULL) {
        s_snapshot.rtc_ready = status->ready;
        s_snapshot.rtc_time_valid = status->time_valid;
        s_snapshot.rtc_oscillator_stopped = status->oscillator_stopped;
        s_snapshot.rtc_eosc_set = status->eosc_set;
        s_snapshot.rtc_epoch_utc = status->epoch_utc;
    } else {
        s_snapshot.rtc_ready = false;
        s_snapshot.rtc_time_valid = false;
        s_snapshot.rtc_eosc_set = false;
    }
    if (error == ESP_OK) {
        s_snapshot.rtc_temperature_c = temperature_c;
        s_snapshot.rtc_consecutive_failures = 0U;
    } else {
        s_snapshot.rtc_last_error = error;
        ++s_snapshot.rtc_consecutive_failures;
    }
    s_snapshot.rtc_last_error = error;
    (void)xSemaphoreGive(s_mutex);
}

static esp_err_t apply_system_epoch(system_time_source_t source,
                                    int64_t epoch_utc_s)
{
    if (!system_time_epoch_is_valid(epoch_utc_s)) {
        return ESP_ERR_INVALID_ARG;
    }
    const struct timeval tv = {
        .tv_sec = (time_t)epoch_utc_s,
        .tv_usec = 0,
    };
    if (settimeofday(&tv, NULL) != 0) {
        return ESP_FAIL;
    }
    return system_time_mark_valid(source, epoch_utc_s);
}

esp_err_t time_service_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buffer);
    }
    if (s_sync_queue == NULL) {
        s_sync_queue = xQueueCreateStatic(
            TIME_SYNC_QUEUE_LEN, sizeof(time_sync_request_t),
            s_sync_queue_storage, &s_sync_queue_buffer);
    }
    if ((s_mutex == NULL) || (s_sync_queue == NULL)) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t time_service_submit_sync(system_time_source_t source,
                                   int64_t epoch_utc_s)
{
    if ((source != SYSTEM_TIME_SOURCE_CELLULAR) &&
        (source != SYSTEM_TIME_SOURCE_REMOTE)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!system_time_epoch_is_valid(epoch_utc_s)) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t init_err = time_service_init();
    if (init_err != ESP_OK) {
        return init_err;
    }
    const time_sync_request_t request = {
        .source = source,
        .epoch_utc_s = epoch_utc_s,
    };
    if (xQueueSend(s_sync_queue, &request, 0) != pdTRUE) {
        time_sync_request_t discarded;
        (void)xQueueReceive(s_sync_queue, &discarded, 0);
        if (xQueueSend(s_sync_queue, &request, 0) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

esp_err_t time_service_apply_rtc_bootstrap(void)
{
    esp_err_t err = ds3231_init();
    if (err != ESP_OK) {
        snapshot_store(NULL, 0.0f, err);
        return err;
    }

    ds3231_status_t status = {0};
    float temperature_c = 0.0f;
    err = ds3231_get_status(&status);
    const esp_err_t temp_err = ds3231_get_temperature(&temperature_c);
    if (err != ESP_OK) {
        snapshot_store(NULL, 0.0f, err);
        return err;
    }
    snapshot_store(&status, temperature_c,
                   (temp_err == ESP_OK) ? ESP_OK : temp_err);

    if (!status.time_valid) {
        return ESP_ERR_INVALID_STATE;
    }
    return apply_system_epoch(SYSTEM_TIME_SOURCE_RTC,
                              (int64_t)status.epoch_utc);
}

esp_err_t time_service_poll_rtc(void)
{
    if (!ds3231_is_ready()) {
        const esp_err_t init_err = ds3231_init();
        if (init_err != ESP_OK) {
            snapshot_store(NULL, 0.0f, init_err);
            return init_err;
        }
    }

    ds3231_status_t status = {0};
    float temperature_c = 0.0f;
    esp_err_t err = ds3231_get_status(&status);
    if (err == ESP_OK) {
        err = ds3231_get_temperature(&temperature_c);
    }
    if (err != ESP_OK) {
        ds3231_invalidate();
        snapshot_store(NULL, 0.0f, err);
        return err;
    }
    snapshot_store(&status, temperature_c, ESP_OK);

    if (!status.time_valid) {
        if (system_time_is_valid()) {
            const time_t now = time(NULL);
            struct tm utc = {0};
            if ((gmtime_r(&now, &utc) != NULL) &&
                (ds3231_set_time(&utc) == ESP_OK)) {
                LOG_INFO(TAG, "RTC corregido desde el reloj UTC vigente");
                return time_service_poll_rtc();
            }
        }
        return ESP_ERR_INVALID_STATE;
    }
    if (!system_time_is_valid()) {
        return apply_system_epoch(SYSTEM_TIME_SOURCE_RTC,
                                  (int64_t)status.epoch_utc);
    }
    return ESP_OK;
}

esp_err_t time_service_process_pending_sync(void)
{
    if (s_sync_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    time_sync_request_t request;
    if (xQueueReceive(s_sync_queue, &request, 0) != pdTRUE) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = apply_system_epoch(request.source, request.epoch_utc_s);
    if (err != ESP_OK) {
        return err;
    }

    if (!ds3231_is_ready()) {
        (void)ds3231_init();
    }
    if (ds3231_is_ready()) {
        const time_t epoch = (time_t)request.epoch_utc_s;
        struct tm utc = {0};
        if (gmtime_r(&epoch, &utc) == NULL) {
            return ESP_FAIL;
        }
        err = ds3231_set_time(&utc);
        if (err != ESP_OK) {
            ds3231_invalidate();
            snapshot_store(NULL, 0.0f, err);
            (void)system_health_report_failure(
                SYSTEM_MODULE_RTC, SYSTEM_EVENT_TIME_SYNC_FAILED,
                SYSTEM_SEVERITY_WARNING, err,
                "RTC rejected authoritative time");
            /* El reloj del sistema ya es valido; el fallo del RTC no lo anula. */
            return ESP_OK;
        }
    }

    (void)time_service_poll_rtc();
    LOG_INFO(TAG, "UTC sincronizado fuente=%s epoch=%lld",
             time_service_source_name(request.source),
             (long long)request.epoch_utc_s);
    return ESP_OK;
}

esp_err_t time_service_get_snapshot(time_service_snapshot_t *out)
{
    if ((out == NULL) || (s_mutex == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    *out = s_snapshot;
    (void)xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t time_service_format_bogota(char *out, size_t out_len,
                                     bool include_seconds)
{
    if ((out == NULL) || (out_len == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    if (!system_time_is_valid()) {
        return ESP_ERR_INVALID_STATE;
    }
    const time_t local_epoch = time(NULL) + (time_t)BOGOTA_OFFSET_SECONDS;
    struct tm local_tm = {0};
    if (gmtime_r(&local_epoch, &local_tm) == NULL) {
        return ESP_FAIL;
    }
    const char *format = include_seconds
        ? "%Y-%m-%d %H:%M:%S" : "%Y-%m-%d %H:%M";
    return (strftime(out, out_len, format, &local_tm) > 0U)
        ? ESP_OK : ESP_ERR_NO_MEM;
}

const char *time_service_source_name(system_time_source_t source)
{
    switch (source) {
        case SYSTEM_TIME_SOURCE_RTC: return "RTC";
        case SYSTEM_TIME_SOURCE_CELLULAR: return "CELLULAR";
        case SYSTEM_TIME_SOURCE_REMOTE: return "REMOTE";
        case SYSTEM_TIME_SOURCE_NONE:
        default: return "NONE";
    }
}
