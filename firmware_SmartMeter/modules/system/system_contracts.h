#ifndef SYSTEM_CONTRACTS_H
#define SYSTEM_CONTRACTS_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYSTEM_CONTRACT_VERSION              1U
#define SYSTEM_EVENT_DETAIL_LEN              48U
#define SYSTEM_DEVICE_ID_LEN                 24U
#define SYSTEM_EVENT_QUEUE_LEN               32U

#define SYSTEM_TIME_VALID_MIN_EPOCH          INT64_C(1577836800)
#define SYSTEM_TIME_VALID_MAX_EPOCH          INT64_C(4102444799)
#define SYSTEM_TIMEZONE_NAME                 "America/Bogota"
#define SYSTEM_TIMEZONE_OFFSET_MIN           (-300)

#define SYSTEM_HISTORY_PERIOD_S              60U
#define SYSTEM_SD_SPACE_WARNING_PCT          80U
#define SYSTEM_SD_SPACE_CRITICAL_PCT         90U
#define SYSTEM_SD_ALERT_RESERVE_PCT          5U

#define SYSTEM_RECOVERY_FAST_RETRIES         3U
#define SYSTEM_RECOVERY_DELAY_1_MS           1000U
#define SYSTEM_RECOVERY_DELAY_2_MS           2000U
#define SYSTEM_RECOVERY_DELAY_3_MS           4000U
#define SYSTEM_RECOVERY_SLOW_PROBE_MS        60000U

typedef enum {
    SYSTEM_MODULE_SYSTEM = 0,
    SYSTEM_MODULE_NVS,
    SYSTEM_MODULE_TIME,
    SYSTEM_MODULE_RTC,
    SYSTEM_MODULE_SD,
    SYSTEM_MODULE_OLED,
    SYSTEM_MODULE_SIM_MODEM,
    SYSTEM_MODULE_CELLULAR,
    SYSTEM_MODULE_MQTT,
    SYSTEM_MODULE_ADE,
    SYSTEM_MODULE_PQ,
    SYSTEM_MODULE_UI,
    SYSTEM_MODULE_INPUT,
    SYSTEM_MODULE_COUNT,
} system_module_t;

typedef enum {
    SYSTEM_STATE_UNINIT = 0,
    SYSTEM_STATE_DISABLED,
    SYSTEM_STATE_RECOVERING,
    SYSTEM_STATE_OK,
    SYSTEM_STATE_DEGRADED,
    SYSTEM_STATE_FAILED,
} system_state_t;

typedef enum {
    SYSTEM_SEVERITY_INFO = 0,
    SYSTEM_SEVERITY_WARNING,
    SYSTEM_SEVERITY_CRITICAL,
} system_severity_t;

typedef enum {
    SYSTEM_EVENT_RAISED = 0,
    SYSTEM_EVENT_CLEARED,
    SYSTEM_EVENT_OCCURRENCE,
} system_event_transition_t;

typedef enum {
    SYSTEM_TIME_SOURCE_NONE = 0,
    SYSTEM_TIME_SOURCE_RTC,
    SYSTEM_TIME_SOURCE_CELLULAR,
    SYSTEM_TIME_SOURCE_REMOTE,
} system_time_source_t;

typedef enum {
    SYSTEM_EVENT_CODE_NONE = 0,
    SYSTEM_EVENT_MODULE_READY,
    SYSTEM_EVENT_MODULE_DISABLED,
    SYSTEM_EVENT_INIT_FAILED,
    SYSTEM_EVENT_IO_ERROR,
    SYSTEM_EVENT_TIMEOUT,
    SYSTEM_EVENT_RECOVERY_FAILED,
    SYSTEM_EVENT_QUEUE_OVERFLOW,
    SYSTEM_EVENT_TASK_STALL,
    SYSTEM_EVENT_MUTEX_ERROR,
    SYSTEM_EVENT_TIME_INVALID,
    SYSTEM_EVENT_TIME_SYNCED,
    SYSTEM_EVENT_TIME_SYNC_FAILED,
    SYSTEM_EVENT_SD_LOW_SPACE,
    SYSTEM_EVENT_SD_FULL,
    SYSTEM_EVENT_DATA_LOSS,
    SYSTEM_EVENT_SIM_NO_AT,
    SYSTEM_EVENT_CELL_NO_SERVICE,
    SYSTEM_EVENT_PDP_DOWN,
    SYSTEM_EVENT_MQTT_DISCONNECTED,
    SYSTEM_EVENT_PQ_SAG,
    SYSTEM_EVENT_PQ_SWELL,
    SYSTEM_EVENT_PQ_FREQ_OOR,
    SYSTEM_EVENT_OVERTEMP,
    SYSTEM_EVENT_SPI_ERROR,
    SYSTEM_EVENT_ADC_ERROR,
    SYSTEM_EVENT_RTC_BACKUP_LOST,
    SYSTEM_EVENT_CODE_COUNT,
} system_event_code_t;

typedef enum {
    SYSTEM_RECORD_MEASUREMENT = 0,
    SYSTEM_RECORD_EVENT,
    SYSTEM_RECORD_TELEMETRY_PENDING,
    SYSTEM_RECORD_ALERT_PENDING,
} system_record_type_t;

typedef struct {
    uint8_t version;
    system_module_t module;
    system_event_code_t code;
    system_severity_t severity;
    system_event_transition_t transition;
    system_state_t resulting_state;
    esp_err_t error;
    uint32_t boot_id;
    uint32_t sequence;
    int64_t timestamp_utc_s;
    int64_t uptime_us;
    uint32_t occurrence_count;
    bool time_valid;
    char detail[SYSTEM_EVENT_DETAIL_LEN];
} system_event_t;

typedef struct {
    system_module_t module;
    system_state_t state;
    system_event_code_t active_event;
    system_severity_t active_severity;
    esp_err_t last_error;
    uint32_t consecutive_failures;
    uint32_t recovery_attempts;
    uint32_t total_failures;
    uint32_t active_occurrences;
    int64_t last_change_uptime_us;
    int64_t last_ok_uptime_us;
} system_health_snapshot_t;

typedef struct {
    bool valid;
    system_time_source_t source;
    int64_t last_sync_epoch_utc_s;
    int64_t sync_uptime_us;
} system_time_status_t;

typedef struct {
    uint8_t schema_version;
    system_record_type_t type;
    uint32_t boot_id;
    uint32_t sequence;
    int64_t timestamp_utc_s;
    int64_t uptime_ms;
    int16_t timezone_offset_min;
    bool time_valid;
    char device_id[SYSTEM_DEVICE_ID_LEN];
} system_record_base_t;

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_CONTRACTS_H */
