#include "storage_service.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "logger.h"
#include "meter_config.h"
#include "meter_data.h"
#include "mqtt_topics.h"
#include "rtos_app_config.h"
#include "sd_storage.h"
#include "system_health.h"

typedef enum {
    STORAGE_REQ_TELEMETRY = 0,
    STORAGE_REQ_ALERT,
    STORAGE_REQ_EVENT,
    STORAGE_REQ_ACK_TELEMETRY,
    STORAGE_REQ_ACK_ALERT,
} storage_request_type_t;

typedef struct {
    storage_request_type_t type;
    char line[STORAGE_SERVICE_PAYLOAD_MAX];
} storage_request_t;

#define STORAGE_NORMAL_QUEUE_LEN 10U
#define STORAGE_HIGH_QUEUE_LEN    8U

static const char *TAG = "storage_service";
static TaskHandle_t s_task_handle;
static StaticTask_t s_task_buffer;
static StackType_t s_task_stack[TASK_STACK_STORAGE];
static StaticQueue_t s_normal_queue_buffer;
static StaticQueue_t s_high_queue_buffer;
static uint8_t s_normal_queue_storage[STORAGE_NORMAL_QUEUE_LEN * sizeof(storage_request_t)];
static uint8_t s_high_queue_storage[STORAGE_HIGH_QUEUE_LEN * sizeof(storage_request_t)];
static QueueHandle_t s_normal_queue;
static QueueHandle_t s_high_queue;
static StaticSemaphore_t s_snapshot_mutex_buffer;
static SemaphoreHandle_t s_snapshot_mutex;
static storage_service_snapshot_t s_snapshot;
static char s_telemetry_head[STORAGE_SERVICE_PAYLOAD_MAX];
static char s_alert_head[STORAGE_SERVICE_PAYLOAD_MAX];
static bool s_telemetry_claimed;
static bool s_alert_claimed;
static bool s_telemetry_head_invalid;
static bool s_alert_head_invalid;
static uint32_t s_mount_failures;
static int64_t s_next_mount_ms;
static bool s_queue_overflow_reported;
static int64_t s_last_usage_ms;
static int64_t s_last_history_ms;

#if defined(STORAGE_SERVICE_TEST_HOOKS) && (STORAGE_SERVICE_TEST_HOOKS != 0)
static bool s_usage_injected;
static uint32_t s_injected_total_mb;
static uint32_t s_injected_free_mb;
#endif

static int64_t storage_now_ms(void)
{
    return esp_timer_get_time() / 1000LL;
}

static void storage_lock(void)
{
    (void)xSemaphoreTake(s_snapshot_mutex, portMAX_DELAY);
}

static void storage_unlock(void)
{
    (void)xSemaphoreGive(s_snapshot_mutex);
}

static void storage_set_mounted(bool mounted)
{
    storage_lock();
    s_snapshot.mounted = mounted;
    storage_unlock();
}

static const char *storage_path_for_kind(storage_record_kind_t kind)
{
    switch (kind) {
        case STORAGE_RECORD_HISTORY:   return METER_SD_HISTORY_FILE;
        case STORAGE_RECORD_TELEMETRY: return METER_SD_TELEMETRY_QUEUE_FILE;
        case STORAGE_RECORD_ALERT:     return METER_SD_ALERT_QUEUE_FILE;
        case STORAGE_RECORD_EVENT:     return METER_SD_EVENT_LOG_FILE;
        default:                       return NULL;
    }
}

static bool storage_line_valid(const char *line)
{
    if (line == NULL) {
        return false;
    }
    const size_t len = strnlen(line, STORAGE_SERVICE_PAYLOAD_MAX);
    return (len > 1U) && (len < STORAGE_SERVICE_PAYLOAD_MAX) &&
           (strchr(line, '\n') == NULL) && (strchr(line, '\r') == NULL);
}

static bool storage_json_complete(storage_request_type_t type, const char *line)
{
    if (!storage_line_valid(line)) {
        return false;
    }
    const size_t len = strlen(line);
    if (type == STORAGE_REQ_EVENT) {
        return (line[0] == '{') && (line[len - 1U] == '}');
    }
    if ((type == STORAGE_REQ_TELEMETRY) || (type == STORAGE_REQ_ALERT)) {
        return ((line[0] == '[') && (line[len - 1U] == ']')) ||
               ((line[0] == '{') && (line[len - 1U] == '}'));
    }
    return true;
}

static esp_err_t storage_enqueue(storage_request_type_t type,
                                 const char *line,
                                 bool high_priority)
{
    if ((s_normal_queue == NULL) || (s_high_queue == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    storage_request_t request = {.type = type};
    if (line != NULL) {
        if (!storage_json_complete(type, line)) {
            return ESP_ERR_INVALID_SIZE;
        }
        (void)snprintf(request.line, sizeof(request.line), "%s", line);
    }
    QueueHandle_t queue = high_priority ? s_high_queue : s_normal_queue;
    if (xQueueSend(queue, &request, 0) != pdTRUE) {
        storage_lock();
        ++s_snapshot.requests_dropped;
        storage_unlock();
        /* No generar otro evento cuando el registro que fallo ya era un
         * evento/alerta: evitar realimentar el despachador con la cola llena. */
        if ((type != STORAGE_REQ_EVENT) && (type != STORAGE_REQ_ALERT) &&
            !s_queue_overflow_reported) {
            s_queue_overflow_reported = true;
            (void)system_health_report_occurrence(
                SYSTEM_MODULE_SD, SYSTEM_EVENT_QUEUE_OVERFLOW,
                SYSTEM_SEVERITY_WARNING, ESP_ERR_NO_MEM,
                "storage request queue full");
        }
        return ESP_ERR_NO_MEM;
    }
    /* Una insercion posterior demuestra que volvio a existir capacidad. La
     * proxima saturacion podra generar una sola ocurrencia nueva. */
    s_queue_overflow_reported = false;
    return ESP_OK;
}

esp_err_t storage_service_submit_telemetry(const char *json_payload)
{
    return storage_enqueue(STORAGE_REQ_TELEMETRY, json_payload, false);
}

esp_err_t storage_service_submit_alert(const char *json_payload)
{
    return storage_enqueue(STORAGE_REQ_ALERT, json_payload, true);
}

esp_err_t storage_service_submit_event(const char *json_line)
{
    return storage_enqueue(STORAGE_REQ_EVENT, json_line, false);
}

esp_err_t storage_service_ack_pending(storage_record_kind_t kind)
{
    if (kind == STORAGE_RECORD_TELEMETRY) {
        return storage_enqueue(STORAGE_REQ_ACK_TELEMETRY, NULL, false);
    }
    if (kind == STORAGE_RECORD_ALERT) {
        return storage_enqueue(STORAGE_REQ_ACK_ALERT, NULL, true);
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t storage_service_get_snapshot(storage_service_snapshot_t *out)
{
    if ((out == NULL) || (s_snapshot_mutex == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    storage_lock();
    *out = s_snapshot;
    out->requests_pending = (uint32_t)uxQueueMessagesWaiting(s_normal_queue) +
                            (uint32_t)uxQueueMessagesWaiting(s_high_queue);
    storage_unlock();
    return ESP_OK;
}

esp_err_t storage_service_peek_pending(storage_record_kind_t kind,
                                       char *out,
                                       size_t out_len)
{
    if ((out == NULL) || (out_len == 0U) || (s_snapshot_mutex == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *source = NULL;
    storage_lock();
    if ((kind == STORAGE_RECORD_TELEMETRY) && s_telemetry_claimed) {
        source = s_telemetry_head;
    } else if ((kind == STORAGE_RECORD_ALERT) && s_alert_claimed) {
        source = s_alert_head;
    }
    if ((source == NULL) || (source[0] == '\0')) {
        storage_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    const size_t len = strlen(source);
    if (len >= out_len) {
        storage_unlock();
        return ESP_ERR_INVALID_SIZE;
    }
    (void)memcpy(out, source, len + 1U);
    storage_unlock();
    return ESP_OK;
}

static void storage_format_local(const system_record_base_t *base,
                                 char *out,
                                 size_t out_len)
{
    if ((base == NULL) || (out == NULL) || (out_len == 0U) ||
        !base->time_valid) {
        if ((out != NULL) && (out_len > 0U)) {
            out[0] = '\0';
        }
        return;
    }
    const time_t local_epoch = (time_t)base->timestamp_utc_s +
        ((time_t)SYSTEM_TIMEZONE_OFFSET_MIN * (time_t)60);
    struct tm local_tm = {0};
    (void)gmtime_r(&local_epoch, &local_tm);
    (void)strftime(out, out_len, "%Y-%m-%dT%H:%M:%S-05:00", &local_tm);
}

static esp_err_t storage_wrap_payload(system_record_type_t type,
                                      const char *type_name,
                                      const char *payload,
                                      char *out,
                                      size_t out_len)
{
    system_record_base_t base;
    esp_err_t err = system_record_base_create(type, &base);
    if (err != ESP_OK) {
        return err;
    }
    char local[40];
    storage_format_local(&base, local, sizeof(local));
    const int n = snprintf(
        out, out_len,
        "{\"v\":%u,\"device\":\"%s\",\"boot\":%lu,\"seq\":%lu,"
        "\"ts_utc\":%lld,\"local\":\"%s\",\"time_valid\":%s,"
        "\"uptime_ms\":%lld,\"type\":\"%s\",\"payload\":%s}",
        (unsigned)base.schema_version, mqtt_topics_device_id(),
        (unsigned long)base.boot_id, (unsigned long)base.sequence,
        (long long)base.timestamp_utc_s, local,
        base.time_valid ? "true" : "false", (long long)base.uptime_ms,
        type_name, payload);
    return ((n > 0) && (((size_t)n + 1U) < out_len))
               ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static void storage_record_write_result(esp_err_t err)
{
    storage_lock();
    if (err == ESP_OK) {
        ++s_snapshot.writes_ok;
    } else {
        ++s_snapshot.writes_failed;
    }
    storage_unlock();
}

static void storage_schedule_recovery(esp_err_t err, const char *detail)
{
    (void)sd_storage_deinit();
    sd_storage_invalidate();
    storage_set_mounted(false);
    ++s_mount_failures;
    uint32_t delay_ms = SYSTEM_RECOVERY_SLOW_PROBE_MS;
    if (s_mount_failures == 1U) {
        delay_ms = SYSTEM_RECOVERY_DELAY_1_MS;
    } else if (s_mount_failures == 2U) {
        delay_ms = SYSTEM_RECOVERY_DELAY_2_MS;
    } else if (s_mount_failures == 3U) {
        delay_ms = SYSTEM_RECOVERY_DELAY_3_MS;
    }
    s_next_mount_ms = storage_now_ms() + (int64_t)delay_ms;
    (void)system_health_report_failure(
        SYSTEM_MODULE_SD, SYSTEM_EVENT_IO_ERROR, SYSTEM_SEVERITY_WARNING,
        err, detail);
}

static esp_err_t storage_recover_known_files(void)
{
    const char *const files[] = {
        METER_SD_HISTORY_FILE,
        METER_SD_TELEMETRY_QUEUE_FILE,
        METER_SD_ALERT_QUEUE_FILE,
        METER_SD_EVENT_LOG_FILE,
    };
    esp_err_t first_error = ESP_OK;
    for (size_t i = 0U; i < (sizeof(files) / sizeof(files[0])); ++i) {
        const esp_err_t err = sd_storage_recover_file(files[i]);
        if (err != ESP_OK) {
            LOG_WARN(TAG, "recovery failed for %s", files[i]);
            if (first_error == ESP_OK) {
                first_error = err;
            }
        }
    }
    return first_error;
}

static bool storage_try_mount(void)
{
    (void)system_health_mark_recovering(SYSTEM_MODULE_SD, "SD mount/recovery");
    const esp_err_t err = sd_storage_init();
    if (err != ESP_OK) {
        storage_schedule_recovery(err, "SD mount failed");
        return false;
    }
    if (sd_storage_ensure_dir(METER_SD_BASE_DIR) != ESP_OK) {
        storage_schedule_recovery(ESP_FAIL, "SD data directory unavailable");
        return false;
    }
    const esp_err_t recovery_err = storage_recover_known_files();
    if (recovery_err != ESP_OK) {
        storage_schedule_recovery(recovery_err, "SD file recovery failed");
        return false;
    }
    s_mount_failures = 0U;
    s_next_mount_ms = 0;
    s_queue_overflow_reported = false;
    storage_set_mounted(true);
    (void)system_health_report_ok(SYSTEM_MODULE_SD, "SD mounted and recovered");
    return true;
}

static esp_err_t storage_get_usage(uint32_t *total_mb, uint32_t *free_mb)
{
#if defined(STORAGE_SERVICE_TEST_HOOKS) && (STORAGE_SERVICE_TEST_HOOKS != 0)
    if (s_usage_injected) {
        *total_mb = s_injected_total_mb;
        *free_mb = s_injected_free_mb;
        return ESP_OK;
    }
#endif
    return sd_storage_get_usage(total_mb, free_mb);
}

static esp_err_t storage_reclaim_space(void)
{
    size_t dropped = 0U;
    size_t remaining = 0U;
    uint64_t dropped_bytes = 0U;
    esp_err_t err = sd_storage_trim_oldest_lines(
        METER_SD_HISTORY_FILE, 1U, METER_SD_HISTORY_PURGE_BATCH,
        &dropped, &dropped_bytes, &remaining);
    if ((err != ESP_OK) && (err != ESP_ERR_NOT_FOUND)) {
        return err;
    }
    if (dropped > 0U) {
        LOG_WARN(TAG, "SD critical: purged %u old history rows (%llu bytes)",
                 (unsigned)dropped, (unsigned long long)dropped_bytes);
        return ESP_OK;
    }

    bool head_claimed;
    storage_lock();
    head_claimed = s_telemetry_claimed;
    storage_unlock();
    err = sd_storage_trim_oldest_lines(
        METER_SD_TELEMETRY_QUEUE_FILE, head_claimed ? 1U : 0U,
        METER_SD_TELEMETRY_PURGE_BATCH, &dropped, &dropped_bytes, &remaining);
    if ((err != ESP_OK) && (err != ESP_ERR_NOT_FOUND)) {
        return err;
    }
    if (dropped == 0U) {
        return ESP_OK;
    }

    storage_lock();
    s_snapshot.telemetry_pending = (uint32_t)remaining + (head_claimed ? 1U : 0U);
    s_snapshot.data_loss_records += (uint32_t)dropped;
    s_snapshot.data_loss_bytes += dropped_bytes;
    storage_unlock();

    char detail[96];
    (void)snprintf(detail, sizeof(detail),
                   "telemetry purged: records=%u bytes=%llu",
                   (unsigned)dropped, (unsigned long long)dropped_bytes);
    (void)system_health_report_occurrence(
        SYSTEM_MODULE_SD, SYSTEM_EVENT_DATA_LOSS, SYSTEM_SEVERITY_CRITICAL,
        ESP_ERR_NO_MEM, detail);
    return ESP_OK;
}

static void storage_refresh_usage(void)
{
    uint32_t total_mb = 0U;
    uint32_t free_mb = 0U;
    const esp_err_t err = storage_get_usage(&total_mb, &free_mb);
    if ((err != ESP_OK) || (total_mb == 0U) || (free_mb > total_mb)) {
        storage_schedule_recovery((err == ESP_OK) ? ESP_FAIL : err,
                                  "SD capacity query failed");
        return;
    }
    const uint8_t used = (uint8_t)(((uint64_t)(total_mb - free_mb) * 100ULL) /
                                   (uint64_t)total_mb);
    storage_lock();
    s_snapshot.total_mb = total_mb;
    s_snapshot.free_mb = free_mb;
    s_snapshot.used_percent = used;
    storage_unlock();
    const bool reserve_reached = free_mb <= METER_SD_ALERT_RESERVE_MB;
    if ((used >= SYSTEM_SD_SPACE_CRITICAL_PCT) || reserve_reached) {
        (void)system_health_report_failure(
            SYSTEM_MODULE_SD, SYSTEM_EVENT_SD_FULL, SYSTEM_SEVERITY_CRITICAL,
            ESP_ERR_NO_MEM, "SD reserve mode: history/telemetry paused");
        const esp_err_t reclaim_err = storage_reclaim_space();
        if (reclaim_err != ESP_OK) {
            storage_schedule_recovery(reclaim_err, "SD retention rewrite failed");
        }
    } else if (used >= SYSTEM_SD_SPACE_WARNING_PCT) {
        (void)system_health_report_failure(
            SYSTEM_MODULE_SD, SYSTEM_EVENT_SD_LOW_SPACE, SYSTEM_SEVERITY_WARNING,
            ESP_ERR_NO_MEM, "SD usage at or above warning threshold");
    } else {
        (void)system_health_report_ok(SYSTEM_MODULE_SD, "SD space available");
    }
}

static bool storage_reserve_available(storage_request_type_t type)
{
    if ((type == STORAGE_REQ_ALERT) || (type == STORAGE_REQ_EVENT)) {
        return true;
    }
    storage_lock();
    const uint8_t used = s_snapshot.used_percent;
    const uint32_t free_mb = s_snapshot.free_mb;
    const uint32_t total_mb = s_snapshot.total_mb;
    storage_unlock();
    return (used < SYSTEM_SD_SPACE_CRITICAL_PCT) &&
           ((total_mb == 0U) || (free_mb > METER_SD_ALERT_RESERVE_MB));
}

static esp_err_t storage_write_request(const storage_request_t *request)
{
    if ((request == NULL) || !storage_reserve_available(request->type)) {
        return ESP_ERR_NO_MEM;
    }
    const char *path = NULL;
    char wrapped[STORAGE_SERVICE_LINE_MAX];
    const char *line = request->line;
    esp_err_t err = ESP_OK;
    switch (request->type) {
        case STORAGE_REQ_TELEMETRY:
            path = METER_SD_TELEMETRY_QUEUE_FILE;
            err = storage_wrap_payload(SYSTEM_RECORD_TELEMETRY_PENDING,
                                       "telemetry_pending", request->line,
                                       wrapped, sizeof(wrapped));
            line = wrapped;
            break;
        case STORAGE_REQ_ALERT:
            path = METER_SD_ALERT_QUEUE_FILE;
            err = storage_wrap_payload(SYSTEM_RECORD_ALERT_PENDING,
                                       "alert_pending", request->line,
                                       wrapped, sizeof(wrapped));
            line = wrapped;
            break;
        case STORAGE_REQ_EVENT:
            path = METER_SD_EVENT_LOG_FILE;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    if (err == ESP_OK) {
        err = sd_storage_append_line(path, line);
    }
    if (err == ESP_OK) {
        storage_lock();
        if (request->type == STORAGE_REQ_TELEMETRY) {
            ++s_snapshot.telemetry_pending;
        } else if (request->type == STORAGE_REQ_ALERT) {
            ++s_snapshot.alerts_pending;
        }
        storage_unlock();
    }
    return err;
}

static esp_err_t storage_extract_payload(const char *line, char *out, size_t out_len)
{
    const char marker[] = "\"payload\":";
    const char *begin = strstr(line, marker);
    const char *end = strrchr(line, '}');
    if ((begin == NULL) || (end == NULL)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    begin += sizeof(marker) - 1U;
    if (end <= begin) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    const size_t len = (size_t)(end - begin);
    if (len >= out_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    (void)memcpy(out, begin, len);
    out[len] = '\0';
    return ESP_OK;
}

static esp_err_t storage_refresh_head(storage_record_kind_t kind)
{
    char line[STORAGE_SERVICE_LINE_MAX];
    char payload[STORAGE_SERVICE_PAYLOAD_MAX];
    const char *path = storage_path_for_kind(kind);
    if (path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t err = sd_storage_peek_line(path, line, sizeof(line));
    bool available = false;
    bool invalid = (err == ESP_ERR_INVALID_SIZE);
    if (err == ESP_OK) {
        available = (storage_extract_payload(line, payload, sizeof(payload)) == ESP_OK);
        invalid = !available;
    } else if ((err != ESP_ERR_NOT_FOUND) && (err != ESP_ERR_INVALID_SIZE)) {
        return err;
    }
    storage_lock();
    char *target = (kind == STORAGE_RECORD_ALERT) ? s_alert_head : s_telemetry_head;
    bool *claimed = (kind == STORAGE_RECORD_ALERT) ? &s_alert_claimed : &s_telemetry_claimed;
    bool *invalid_latched = (kind == STORAGE_RECORD_ALERT)
        ? &s_alert_head_invalid : &s_telemetry_head_invalid;
    const bool newly_invalid = invalid && !*invalid_latched;
    *invalid_latched = invalid;
    if (!*claimed) {
        if (available) {
            (void)snprintf(target, STORAGE_SERVICE_PAYLOAD_MAX, "%s", payload);
            *claimed = true;
            if (kind == STORAGE_RECORD_ALERT) {
                if (s_snapshot.alerts_pending == 0U) {
                    s_snapshot.alerts_pending = 1U;
                }
            } else if (s_snapshot.telemetry_pending == 0U) {
                s_snapshot.telemetry_pending = 1U;
            }
        } else {
            target[0] = '\0';
        }
    }
    storage_unlock();
    if (newly_invalid) {
        LOG_WARN(TAG, "invalid/truncated JSONL head in %s", path);
        (void)system_health_report_occurrence(
            SYSTEM_MODULE_SD, SYSTEM_EVENT_IO_ERROR, SYSTEM_SEVERITY_WARNING,
            ESP_ERR_INVALID_RESPONSE, "invalid/truncated JSONL head");
    }
    return ESP_OK;
}

static esp_err_t storage_ack(storage_record_kind_t kind)
{
    const char *path = storage_path_for_kind(kind);
    size_t remaining = 0U;
    const esp_err_t err = sd_storage_drop_first_line(path, &remaining);
    storage_lock();
    char *head = (kind == STORAGE_RECORD_ALERT) ? s_alert_head : s_telemetry_head;
    bool *claimed = (kind == STORAGE_RECORD_ALERT) ? &s_alert_claimed : &s_telemetry_claimed;
    head[0] = '\0';
    *claimed = false;
    if ((err == ESP_OK) || (err == ESP_ERR_NOT_FOUND)) {
        if (kind == STORAGE_RECORD_ALERT) {
            s_snapshot.alerts_pending = (uint32_t)remaining;
        } else {
            s_snapshot.telemetry_pending = (uint32_t)remaining;
        }
    }
    storage_unlock();
    return (err == ESP_ERR_NOT_FOUND) ? ESP_OK : err;
}

static esp_err_t storage_write_history(void)
{
    static const char header[] =
        "schema,device,boot_id,sequence,timestamp_utc,local_bogota,time_valid,"
        "uptime_ms,type,vrms,irms_a,p_w,q_var,s_va,pf,f_hz,e_wh,temp_c,pq";
    uint64_t size = 0U;
    esp_err_t err = sd_storage_get_file_size(METER_SD_HISTORY_FILE, &size);
    if ((err == ESP_ERR_NOT_FOUND) || ((err == ESP_OK) && (size == 0U))) {
        err = sd_storage_append_line(METER_SD_HISTORY_FILE, header);
    }
    if (err != ESP_OK) {
        return err;
    }
    system_record_base_t base;
    err = system_record_base_create(SYSTEM_RECORD_MEASUREMENT, &base);
    if (err != ESP_OK) {
        return err;
    }
    const MeterData_t meter = meter_data_get_snapshot();
    char local[40];
    char row[STORAGE_SERVICE_LINE_MAX];
    storage_format_local(&base, local, sizeof(local));
    const int n = snprintf(
        row, sizeof(row),
        "%u,%s,%lu,%lu,%lld,%s,%s,%lld,measurement,%.3f,%.4f,%.3f,"
        "%.3f,%.3f,%.4f,%.3f,%.4f,%.2f,%lu",
        (unsigned)base.schema_version, mqtt_topics_device_id(),
        (unsigned long)base.boot_id, (unsigned long)base.sequence,
        (long long)base.timestamp_utc_s, local,
        base.time_valid ? "true" : "false", (long long)base.uptime_ms,
        (double)meter.vrms, (double)meter.irms_a,
        (double)meter.active_power_w, (double)meter.reactive_power_var,
        (double)meter.apparent_power_va, (double)meter.power_factor,
        (double)meter.frequency, (double)meter.energy_total_nvs_wh,
        (double)meter.temperature, (unsigned long)meter.pq_status);
    if ((n <= 0) || ((size_t)n >= sizeof(row))) {
        return ESP_ERR_INVALID_SIZE;
    }
    return sd_storage_append_line(METER_SD_HISTORY_FILE, row);
}

static void storage_process_request(const storage_request_t *request)
{
    esp_err_t err;
    if (request->type == STORAGE_REQ_ACK_TELEMETRY) {
        err = storage_ack(STORAGE_RECORD_TELEMETRY);
    } else if (request->type == STORAGE_REQ_ACK_ALERT) {
        err = storage_ack(STORAGE_RECORD_ALERT);
    } else {
        err = storage_write_request(request);
    }
    storage_record_write_result(err);
    if (err == ESP_ERR_NO_MEM) {
        storage_lock();
        ++s_snapshot.requests_dropped;
        storage_unlock();
        return;
    }
    if (err != ESP_OK) {
        storage_schedule_recovery(err, "SD queued operation failed");
    }
}

static void task_storage(void *arg)
{
    (void)arg;
    for (;;) {
        const int64_t now = storage_now_ms();
        if (!sd_storage_is_mounted()) {
            if (now >= s_next_mount_ms) {
                (void)storage_try_mount();
            }
            vTaskDelay(pdMS_TO_TICKS(100U));
            continue;
        }
        if ((now - s_last_usage_ms) >= (int64_t)METER_SD_USAGE_REFRESH_MS) {
            storage_refresh_usage();
            s_last_usage_ms = now;
            if (!sd_storage_is_mounted()) {
                continue;
            }
        }
        if ((now - s_last_history_ms) >= (int64_t)METER_SD_HISTORY_PERIOD_MS) {
            esp_err_t err = ESP_ERR_NO_MEM;
            if (storage_reserve_available(STORAGE_REQ_TELEMETRY)) {
                err = storage_write_history();
            }
            s_last_history_ms = now;
            if (err == ESP_ERR_NO_MEM) {
                storage_lock();
                ++s_snapshot.requests_dropped;
                storage_unlock();
            } else {
                storage_record_write_result(err);
            }
            if ((err != ESP_OK) && (err != ESP_ERR_NO_MEM)) {
                storage_schedule_recovery(err, "SD history write failed");
                continue;
            }
        }
        storage_request_t request;
        if ((xQueueReceive(s_high_queue, &request, 0) == pdTRUE) ||
            (xQueueReceive(s_normal_queue, &request, 0) == pdTRUE)) {
            storage_process_request(&request);
            continue;
        }
        esp_err_t head_err = storage_refresh_head(STORAGE_RECORD_ALERT);
        if (head_err == ESP_OK) {
            head_err = storage_refresh_head(STORAGE_RECORD_TELEMETRY);
        }
        if (head_err != ESP_OK) {
            storage_schedule_recovery(head_err, "SD queue read failed");
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(50U));
    }
}

esp_err_t storage_service_start(void)
{
    if (s_task_handle != NULL) {
        return ESP_OK;
    }
    if (!system_health_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    s_snapshot_mutex = xSemaphoreCreateMutexStatic(&s_snapshot_mutex_buffer);
    s_normal_queue = xQueueCreateStatic(STORAGE_NORMAL_QUEUE_LEN,
                                        sizeof(storage_request_t),
                                        s_normal_queue_storage,
                                        &s_normal_queue_buffer);
    s_high_queue = xQueueCreateStatic(STORAGE_HIGH_QUEUE_LEN,
                                      sizeof(storage_request_t),
                                      s_high_queue_storage,
                                      &s_high_queue_buffer);
    if ((s_snapshot_mutex == NULL) || (s_normal_queue == NULL) ||
        (s_high_queue == NULL)) {
        return ESP_ERR_NO_MEM;
    }
    (void)memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_next_mount_ms = 0;
    s_task_handle = xTaskCreateStaticPinnedToCore(
        task_storage, "task_storage", TASK_STACK_STORAGE, NULL,
        TASK_PRIO_STORAGE, s_task_stack, &s_task_buffer, tskNO_AFFINITY);
    if (s_task_handle == NULL) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

#if defined(STORAGE_SERVICE_TEST_HOOKS) && (STORAGE_SERVICE_TEST_HOOKS != 0)
void storage_service_test_inject_usage(uint32_t total_mb, uint32_t free_mb)
{
    s_injected_total_mb = total_mb;
    s_injected_free_mb = free_mb;
    s_usage_injected = true;
}

void storage_service_test_clear_usage(void)
{
    s_usage_injected = false;
}
#endif
