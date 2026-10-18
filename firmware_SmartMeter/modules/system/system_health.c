#include "system_health.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "logger.h"

static const char *TAG = "system_health";

static system_health_snapshot_t s_health[SYSTEM_MODULE_COUNT];
static system_time_status_t s_time_status;
static uint32_t s_boot_id;
static uint32_t s_sequence;
static uint32_t s_dropped_total;
static uint32_t s_overflow_unreported;
static bool s_initialized;

static StaticSemaphore_t s_mutex_buffer;
static SemaphoreHandle_t s_mutex;
static StaticQueue_t s_event_queue_buffer;
static uint8_t s_event_queue_storage[SYSTEM_EVENT_QUEUE_LEN * sizeof(system_event_t)];
static QueueHandle_t s_event_queue;
static system_event_t s_queue_scratch[SYSTEM_EVENT_QUEUE_LEN];

static bool module_valid(system_module_t module)
{
    return (module >= SYSTEM_MODULE_SYSTEM) && (module < SYSTEM_MODULE_COUNT);
}

static bool severity_valid(system_severity_t severity)
{
    return (severity >= SYSTEM_SEVERITY_INFO) &&
           (severity <= SYSTEM_SEVERITY_CRITICAL);
}

static void copy_detail(char dst[SYSTEM_EVENT_DETAIL_LEN], const char *src)
{
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    (void)snprintf(dst, SYSTEM_EVENT_DETAIL_LEN, "%s", src);
}

bool system_time_epoch_is_valid(int64_t epoch_utc_s)
{
    return (epoch_utc_s >= SYSTEM_TIME_VALID_MIN_EPOCH) &&
           (epoch_utc_s <= SYSTEM_TIME_VALID_MAX_EPOCH);
}

static void fill_event_locked(system_event_t *event,
                              system_module_t module,
                              system_event_code_t code,
                              system_severity_t severity,
                              system_event_transition_t transition,
                              system_state_t resulting_state,
                              esp_err_t error,
                              uint32_t occurrence_count,
                              const char *detail)
{
    (void)memset(event, 0, sizeof(*event));
    event->version = SYSTEM_CONTRACT_VERSION;
    event->module = module;
    event->code = code;
    event->severity = severity;
    event->transition = transition;
    event->resulting_state = resulting_state;
    event->error = error;
    event->boot_id = s_boot_id;
    event->sequence = ++s_sequence;
    event->uptime_us = esp_timer_get_time();

    const int64_t now = (int64_t)time(NULL);
    event->time_valid = s_time_status.valid && system_time_epoch_is_valid(now);
    event->timestamp_utc_s = event->time_valid ? now : 0;
    event->occurrence_count = occurrence_count;
    copy_detail(event->detail, detail);
}

/* Called with s_mutex held. */
static bool enqueue_with_priority_locked(const system_event_t *event)
{
    if (xQueueSend(s_event_queue, event, 0) == pdTRUE) {
        return true;
    }

    if (event->severity == SYSTEM_SEVERITY_INFO) {
        ++s_dropped_total;
        ++s_overflow_unreported;
        return false;
    }

    size_t count = 0U;
    while ((count < SYSTEM_EVENT_QUEUE_LEN) &&
           (xQueueReceive(s_event_queue, &s_queue_scratch[count], 0) == pdTRUE)) {
        ++count;
    }

    size_t candidate = count;
    system_severity_t lowest = SYSTEM_SEVERITY_CRITICAL;
    for (size_t i = 0U; i < count; ++i) {
        if (s_queue_scratch[i].severity <= event->severity) {
            if ((candidate == count) || (s_queue_scratch[i].severity < lowest)) {
                candidate = i;
                lowest = s_queue_scratch[i].severity;
            }
        }
    }

    for (size_t i = 0U; i < count; ++i) {
        if (i != candidate) {
            (void)xQueueSend(s_event_queue, &s_queue_scratch[i], 0);
        }
    }

    if (candidate < count) {
        ++s_dropped_total;
        ++s_overflow_unreported;
        return xQueueSend(s_event_queue, event, 0) == pdTRUE;
    }

    ++s_dropped_total;
    ++s_overflow_unreported;
    return false;
}

static void enqueue_event_locked(system_module_t module,
                                 system_event_code_t code,
                                 system_severity_t severity,
                                 system_event_transition_t transition,
                                 system_state_t resulting_state,
                                 esp_err_t error,
                                 uint32_t occurrence_count,
                                 const char *detail)
{
    system_event_t event;
    fill_event_locked(&event, module, code, severity, transition,
                      resulting_state, error, occurrence_count, detail);
    (void)enqueue_with_priority_locked(&event);
}

esp_err_t system_health_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buffer);
    s_event_queue = xQueueCreateStatic(SYSTEM_EVENT_QUEUE_LEN,
                                       sizeof(system_event_t),
                                       s_event_queue_storage,
                                       &s_event_queue_buffer);
    if ((s_mutex == NULL) || (s_event_queue == NULL)) {
        return ESP_ERR_NO_MEM;
    }

    (void)memset(s_health, 0, sizeof(s_health));
    for (int i = 0; i < (int)SYSTEM_MODULE_COUNT; ++i) {
        s_health[i].module = (system_module_t)i;
        s_health[i].state = SYSTEM_STATE_UNINIT;
        s_health[i].active_event = SYSTEM_EVENT_CODE_NONE;
    }
    (void)memset(&s_time_status, 0, sizeof(s_time_status));
    s_boot_id = esp_random();
    if (s_boot_id == 0U) {
        s_boot_id = 1U;
    }
    s_sequence = 0U;
    s_dropped_total = 0U;
    s_overflow_unreported = 0U;
    s_initialized = true;

    s_health[SYSTEM_MODULE_SYSTEM].state = SYSTEM_STATE_OK;
    s_health[SYSTEM_MODULE_SYSTEM].last_ok_uptime_us = esp_timer_get_time();
    LOG_INFO(TAG, "contratos v%u activos boot_id=%08lx queue=%u",
             (unsigned)SYSTEM_CONTRACT_VERSION,
             (unsigned long)s_boot_id,
             (unsigned)SYSTEM_EVENT_QUEUE_LEN);
    return ESP_OK;
}

bool system_health_is_initialized(void)
{
    return s_initialized;
}

esp_err_t system_health_set_disabled(system_module_t module, const char *detail)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!module_valid(module)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    system_health_snapshot_t *health = &s_health[module];
    const bool changed = (health->state != SYSTEM_STATE_DISABLED);
    const system_event_code_t active = health->active_event;
    const system_severity_t active_severity = health->active_severity;
    const uint32_t active_occurrences = health->active_occurrences;
    health->state = SYSTEM_STATE_DISABLED;
    health->active_event = SYSTEM_EVENT_CODE_NONE;
    health->active_severity = SYSTEM_SEVERITY_INFO;
    health->last_error = ESP_OK;
    health->consecutive_failures = 0U;
    health->recovery_attempts = 0U;
    health->active_occurrences = 0U;
    if (changed || (active != SYSTEM_EVENT_CODE_NONE)) {
        health->last_change_uptime_us = esp_timer_get_time();
    }
    if (active != SYSTEM_EVENT_CODE_NONE) {
        enqueue_event_locked(module, active, active_severity,
                             SYSTEM_EVENT_CLEARED, SYSTEM_STATE_DISABLED,
                             ESP_OK,
                             (active_occurrences > 0U) ? active_occurrences : 1U,
                             "module disabled; active fault cleared");
    }
    if (changed) {
        enqueue_event_locked(module, SYSTEM_EVENT_MODULE_DISABLED,
                             SYSTEM_SEVERITY_INFO, SYSTEM_EVENT_OCCURRENCE,
                             SYSTEM_STATE_DISABLED, ESP_OK, 1U, detail);
    }
    (void)xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t system_health_mark_recovering(system_module_t module, const char *detail)
{
    (void)detail;
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!module_valid(module)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    system_health_snapshot_t *health = &s_health[module];
    const bool changed = (health->state != SYSTEM_STATE_RECOVERING);
    health->state = SYSTEM_STATE_RECOVERING;
    ++health->recovery_attempts;
    if (changed) {
        health->last_change_uptime_us = esp_timer_get_time();
    }
    (void)xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t system_health_report_failure(system_module_t module,
                                       system_event_code_t code,
                                       system_severity_t severity,
                                       esp_err_t error,
                                       const char *detail)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!module_valid(module) || (code <= SYSTEM_EVENT_CODE_NONE) ||
        (code >= SYSTEM_EVENT_CODE_COUNT) || !severity_valid(severity)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    system_health_snapshot_t *health = &s_health[module];
    const system_state_t previous_state = health->state;
    const system_event_code_t previous_event = health->active_event;
    const system_severity_t previous_severity = health->active_severity;
    ++health->consecutive_failures;
    ++health->total_failures;
    health->last_error = error;
    health->state = (health->consecutive_failures >= SYSTEM_RECOVERY_FAST_RETRIES)
                        ? SYSTEM_STATE_FAILED
                        : SYSTEM_STATE_DEGRADED;
    if ((health->state != previous_state) || (code != previous_event) ||
        (severity > previous_severity)) {
        health->last_change_uptime_us = esp_timer_get_time();
    }

    if (health->active_event == code) {
        ++health->active_occurrences;
        if (severity > health->active_severity) {
            health->active_severity = severity;
        }
    } else {
        health->active_event = code;
        health->active_severity = severity;
        health->active_occurrences = 1U;
        enqueue_event_locked(module, code, severity, SYSTEM_EVENT_RAISED,
                             health->state, error, 1U, detail);
    }

    (void)xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t system_health_report_ok(system_module_t module, const char *detail)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!module_valid(module)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    system_health_snapshot_t *health = &s_health[module];
    const system_state_t previous_state = health->state;
    const system_event_code_t active = health->active_event;
    const system_severity_t severity = health->active_severity;
    const uint32_t occurrences = health->active_occurrences;

    health->state = SYSTEM_STATE_OK;
    health->active_event = SYSTEM_EVENT_CODE_NONE;
    health->active_severity = SYSTEM_SEVERITY_INFO;
    health->last_error = ESP_OK;
    health->consecutive_failures = 0U;
    health->recovery_attempts = 0U;
    health->active_occurrences = 0U;
    const int64_t now_us = esp_timer_get_time();
    if ((previous_state != SYSTEM_STATE_OK) ||
        (active != SYSTEM_EVENT_CODE_NONE)) {
        health->last_change_uptime_us = now_us;
    }
    health->last_ok_uptime_us = now_us;

    if (active != SYSTEM_EVENT_CODE_NONE) {
        enqueue_event_locked(module, active, severity, SYSTEM_EVENT_CLEARED,
                             SYSTEM_STATE_OK, ESP_OK,
                             (occurrences > 0U) ? occurrences : 1U, detail);
    } else if ((previous_state == SYSTEM_STATE_UNINIT) ||
               (previous_state == SYSTEM_STATE_RECOVERING)) {
        enqueue_event_locked(module, SYSTEM_EVENT_MODULE_READY,
                             SYSTEM_SEVERITY_INFO, SYSTEM_EVENT_OCCURRENCE,
                             SYSTEM_STATE_OK, ESP_OK, 1U, detail);
    }

    (void)xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t system_health_report_occurrence(system_module_t module,
                                          system_event_code_t code,
                                          system_severity_t severity,
                                          esp_err_t error,
                                          const char *detail)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!module_valid(module) || (code <= SYSTEM_EVENT_CODE_NONE) ||
        (code >= SYSTEM_EVENT_CODE_COUNT) || !severity_valid(severity)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    enqueue_event_locked(module, code, severity, SYSTEM_EVENT_OCCURRENCE,
                         s_health[module].state, error, 1U, detail);
    (void)xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t system_health_get_snapshot(system_module_t module,
                                     system_health_snapshot_t *out)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!module_valid(module) || (out == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    *out = s_health[module];
    (void)xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t system_health_get_all_snapshots(system_health_snapshot_t *out,
                                          size_t count)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((out == NULL) || (count < SYSTEM_MODULE_COUNT)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    (void)memcpy(out, s_health, sizeof(s_health));
    (void)xSemaphoreGive(s_mutex);
    return ESP_OK;
}

bool system_health_has_active_critical(void)
{
    if (!s_initialized || (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE)) {
        return false;
    }
    bool found = false;
    for (int i = 0; i < (int)SYSTEM_MODULE_COUNT; ++i) {
        if ((s_health[i].active_event != SYSTEM_EVENT_CODE_NONE) &&
            (s_health[i].active_severity == SYSTEM_SEVERITY_CRITICAL)) {
            found = true;
            break;
        }
    }
    (void)xSemaphoreGive(s_mutex);
    return found;
}

static void enqueue_overflow_notice_if_possible(void)
{
    if (!s_initialized || (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE)) {
        return;
    }
    if ((s_overflow_unreported > 0U) &&
        (uxQueueSpacesAvailable(s_event_queue) > 0U)) {
        const uint32_t dropped = s_overflow_unreported;
        s_overflow_unreported = 0U;
        system_event_t overflow;
        fill_event_locked(&overflow, SYSTEM_MODULE_SYSTEM,
                          SYSTEM_EVENT_QUEUE_OVERFLOW,
                          SYSTEM_SEVERITY_WARNING,
                          SYSTEM_EVENT_OCCURRENCE,
                          s_health[SYSTEM_MODULE_SYSTEM].state,
                          ESP_ERR_NO_MEM, dropped,
                          "event queue saturated");
        if (xQueueSend(s_event_queue, &overflow, 0) != pdTRUE) {
            s_overflow_unreported += dropped;
        }
    }
    (void)xSemaphoreGive(s_mutex);
}

esp_err_t system_event_receive(system_event_t *out, TickType_t wait_ticks)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xQueueReceive(s_event_queue, out, wait_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    enqueue_overflow_notice_if_possible();
    return ESP_OK;
}

uint32_t system_event_get_dropped_count(void)
{
    if (!s_initialized || (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE)) {
        return 0U;
    }
    const uint32_t value = s_dropped_total;
    (void)xSemaphoreGive(s_mutex);
    return value;
}

uint32_t system_event_get_pending_count(void)
{
    return (s_event_queue != NULL) ? (uint32_t)uxQueueMessagesWaiting(s_event_queue) : 0U;
}

esp_err_t system_time_mark_valid(system_time_source_t source,
                                 int64_t epoch_utc_s)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((source <= SYSTEM_TIME_SOURCE_NONE) ||
        (source > SYSTEM_TIME_SOURCE_REMOTE) ||
        !system_time_epoch_is_valid(epoch_utc_s)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    s_time_status.valid = true;
    s_time_status.source = source;
    s_time_status.last_sync_epoch_utc_s = epoch_utc_s;
    s_time_status.sync_uptime_us = esp_timer_get_time();
    (void)xSemaphoreGive(s_mutex);

    const esp_err_t ok_err = system_health_report_ok(SYSTEM_MODULE_TIME,
                                                     "UTC time valid");
    if (ok_err != ESP_OK) {
        return ok_err;
    }
    return system_health_report_occurrence(SYSTEM_MODULE_TIME,
                                           SYSTEM_EVENT_TIME_SYNCED,
                                           SYSTEM_SEVERITY_INFO,
                                           ESP_OK,
                                           "UTC clock synchronized");
}

void system_time_mark_invalid(const char *detail)
{
    if (!s_initialized || (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE)) {
        return;
    }
    s_time_status.valid = false;
    s_time_status.source = SYSTEM_TIME_SOURCE_NONE;
    s_time_status.last_sync_epoch_utc_s = 0;
    s_time_status.sync_uptime_us = esp_timer_get_time();
    (void)xSemaphoreGive(s_mutex);
    (void)system_health_report_failure(SYSTEM_MODULE_TIME,
                                       SYSTEM_EVENT_TIME_INVALID,
                                       SYSTEM_SEVERITY_WARNING,
                                       ESP_ERR_INVALID_STATE,
                                       detail);
}

bool system_time_is_valid(void)
{
    if (!s_initialized || (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE)) {
        return false;
    }
    const bool valid = s_time_status.valid &&
                       system_time_epoch_is_valid((int64_t)time(NULL));
    (void)xSemaphoreGive(s_mutex);
    return valid;
}

esp_err_t system_time_get_status(system_time_status_t *out)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    *out = s_time_status;
    (void)xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t system_record_base_create(system_record_type_t type,
                                    system_record_base_t *out)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((out == NULL) || (type < SYSTEM_RECORD_MEASUREMENT) ||
        (type > SYSTEM_RECORD_ALERT_PENDING)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    (void)memset(out, 0, sizeof(*out));
    out->schema_version = SYSTEM_CONTRACT_VERSION;
    out->type = type;
    out->boot_id = s_boot_id;
    out->sequence = ++s_sequence;
    out->uptime_ms = esp_timer_get_time() / 1000LL;
    out->timezone_offset_min = SYSTEM_TIMEZONE_OFFSET_MIN;
    const int64_t now = (int64_t)time(NULL);
    out->time_valid = s_time_status.valid && system_time_epoch_is_valid(now);
    out->timestamp_utc_s = out->time_valid ? now : 0;
    (void)xSemaphoreGive(s_mutex);
    return ESP_OK;
}

const char *system_module_name(system_module_t module)
{
    static const char *const names[] = {
        "SYSTEM", "NVS", "TIME", "RTC", "SD", "OLED", "SIM_MODEM",
        "CELLULAR", "MQTT", "ADE", "PQ", "UI", "INPUT"
    };
    return module_valid(module) ? names[module] : "UNKNOWN";
}

const char *system_state_name(system_state_t state)
{
    static const char *const names[] = {
        "UNINIT", "DISABLED", "RECOVERING", "OK", "DEGRADED", "FAILED"
    };
    return ((unsigned)state < (sizeof(names) / sizeof(names[0])))
               ? names[state] : "UNKNOWN";
}

const char *system_severity_name(system_severity_t severity)
{
    static const char *const names[] = {"INFO", "WARNING", "CRITICAL"};
    return ((unsigned)severity < (sizeof(names) / sizeof(names[0])))
               ? names[severity] : "UNKNOWN";
}

const char *system_event_transition_name(system_event_transition_t transition)
{
    static const char *const names[] = {"RAISED", "CLEARED", "OCCURRENCE"};
    return ((unsigned)transition < (sizeof(names) / sizeof(names[0])))
               ? names[transition] : "UNKNOWN";
}

const char *system_event_code_name(system_event_code_t code)
{
    static const char *const names[] = {
        "NONE", "MODULE_READY", "MODULE_DISABLED", "INIT_FAILED",
        "IO_ERROR", "TIMEOUT", "RECOVERY_FAILED", "EVENT_QUEUE_OVERFLOW",
        "TASK_STALL", "MUTEX_ERROR", "TIME_INVALID", "TIME_SYNCED",
        "TIME_SYNC_FAILED", "SD_LOW_SPACE", "SD_FULL", "DATA_LOSS",
        "SIM_NO_AT", "CELL_NO_SERVICE", "PDP_DOWN", "MQTT_DISCONNECTED",
        "PQ_SAG", "PQ_SWELL", "PQ_FREQ_OOR", "OVERTEMP", "SPI_ERROR",
        "ADC_ERROR", "RTC_BACKUP_LOST"
    };
    return ((code >= SYSTEM_EVENT_CODE_NONE) && (code < SYSTEM_EVENT_CODE_COUNT))
               ? names[code] : "UNKNOWN";
}
