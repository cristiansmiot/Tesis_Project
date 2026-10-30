#include "system_contracts_test.h"

#include <sys/time.h>
#include <string.h>
#include <math.h>

#include "ds3231.h"
#include "sim7080g_radio.h"
#include "data_serializer.h"
#include "node_health.h"
#include "storage_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger.h"
#include "system_health.h"

static const char *TAG = "contracts_test";

#define TEST_CHECK(condition, message)                          \
    do {                                                        \
        if (!(condition)) {                                     \
            LOG_ERROR(TAG, "FAIL line=%d: %s", __LINE__, message); \
            return false;                                       \
        }                                                       \
    } while (0)

static void drain_events(void)
{
    system_event_t event;
    while (system_event_receive(&event, 0) == ESP_OK) {
    }
}

typedef struct {
    TaskHandle_t parent;
    unsigned iterations;
} producer_ctx_t;

static void concurrent_producer(void *arg)
{
    const producer_ctx_t *ctx = (const producer_ctx_t *)arg;
    for (unsigned i = 0U; i < ctx->iterations; ++i) {
        (void)system_health_report_failure(
            SYSTEM_MODULE_NVS, SYSTEM_EVENT_IO_ERROR,
            SYSTEM_SEVERITY_WARNING, ESP_FAIL, "concurrent producer");
    }
    xTaskNotifyGive(ctx->parent);
    vTaskDelete(NULL);
}

bool system_contracts_test_run(void)
{
    sim7080g_radio_t radio;
    TEST_CHECK(sim7080g_radio_parse_cpsi(
        "\r\n+CPSI: LTE NB-IOT,Online,460-11,0x5AE1,187212754,82,EUTRAN-BAND5,2506,0,0,-7,-115,-110,13\r\nOK\r\n", &radio),
        "CPSI manual example rejected");
    TEST_CHECK(radio.valid && radio.rsrp_dbm == -115.0f &&
        radio.rsrq_db == -7.0f && radio.sinr_db == 6.0f, "CPSI units/formula");
    TEST_CHECK(!sim7080g_radio_parse_cpsi("+CPSI: NO SERVICE,Online\r\nOK", &radio)
        && !radio.valid, "CPSI no-service reused old values");
    TEST_CHECK(!sim7080g_radio_parse_cpsi(
        "+CPSI: LTE CAT-M1,Online,732-123,1,2,3,4,5,0,0, ,-115,-110,13", &radio),
        "CPSI empty value accepted");
    TEST_CHECK(!sim7080g_radio_parse_cpsi(
        "+CPSI: LTE CAT-M1,Online,732-123,1,2,3,4,5,0,0,-70,-1150,-1100,130", &radio),
        "CPSI undocumented scaled values accepted");
    struct tm epoch_test = {
        .tm_year = 120,
        .tm_mon = 0,
        .tm_mday = 1,
    };
    time_t converted_epoch = 0;
    TEST_CHECK(ds3231_tm_to_epoch_utc(&epoch_test, &converted_epoch) == ESP_OK,
               "UTC calendar conversion failed");
    TEST_CHECK((int64_t)converted_epoch == SYSTEM_TIME_VALID_MIN_EPOCH,
               "UTC conversion depends on local TZ");
    epoch_test.tm_year = 123;
    epoch_test.tm_mon = 1;
    epoch_test.tm_mday = 29;
    TEST_CHECK(ds3231_tm_to_epoch_utc(&epoch_test, &converted_epoch) ==
                   ESP_ERR_INVALID_ARG,
               "invalid calendar accepted");

    TEST_CHECK(system_health_init() == ESP_OK, "system_health_init");
    drain_events();

    system_health_snapshot_t health;
    TEST_CHECK(system_health_get_snapshot(SYSTEM_MODULE_OLED, &health) == ESP_OK,
               "get initial OLED snapshot");
    TEST_CHECK(health.state == SYSTEM_STATE_UNINIT,
               "initial state must be UNINIT");

    TEST_CHECK(system_health_set_disabled(SYSTEM_MODULE_ADE,
                                          "test profile") == ESP_OK,
               "set ADE disabled");
    TEST_CHECK(system_health_get_snapshot(SYSTEM_MODULE_ADE, &health) == ESP_OK,
               "get ADE snapshot");
    TEST_CHECK(health.state == SYSTEM_STATE_DISABLED, "ADE must be DISABLED");
    system_event_t event;
    TEST_CHECK(system_event_receive(&event, 0) == ESP_OK,
               "disabled event missing");
    TEST_CHECK((event.module == SYSTEM_MODULE_ADE) &&
               (event.code == SYSTEM_EVENT_MODULE_DISABLED),
               "disabled event mismatch");

    TEST_CHECK(system_health_mark_recovering(SYSTEM_MODULE_RTC,
                                             "test recovery") == ESP_OK,
               "RTC recovering");
    TEST_CHECK(system_health_get_snapshot(SYSTEM_MODULE_RTC, &health) == ESP_OK,
               "get recovering RTC snapshot");
    TEST_CHECK(health.state == SYSTEM_STATE_RECOVERING,
               "RTC must be RECOVERING");
    for (unsigned i = 0U; i < SYSTEM_RECOVERY_FAST_RETRIES; ++i) {
        TEST_CHECK(system_health_report_failure(
                       SYSTEM_MODULE_RTC, SYSTEM_EVENT_IO_ERROR,
                       SYSTEM_SEVERITY_WARNING, ESP_FAIL,
                       "repeated RTC error") == ESP_OK,
                   "RTC failure report");
        if (i == 0U) {
            TEST_CHECK(system_health_get_snapshot(SYSTEM_MODULE_RTC,
                                                  &health) == ESP_OK,
                       "get degraded RTC snapshot");
            TEST_CHECK(health.state == SYSTEM_STATE_DEGRADED,
                       "RTC must first be DEGRADED");
        }
    }
    TEST_CHECK(system_health_get_snapshot(SYSTEM_MODULE_RTC, &health) == ESP_OK,
               "get RTC snapshot");
    TEST_CHECK(health.state == SYSTEM_STATE_FAILED, "RTC must reach FAILED");
    TEST_CHECK(health.active_occurrences == SYSTEM_RECOVERY_FAST_RETRIES,
               "repeated fault occurrence count");
    TEST_CHECK(system_event_get_pending_count() == 1U,
               "repeated fault flooded queue");
    TEST_CHECK(system_event_receive(&event, 0) == ESP_OK,
               "RTC RAISED missing");
    TEST_CHECK(event.transition == SYSTEM_EVENT_RAISED,
               "RTC event must be RAISED");

    TEST_CHECK(system_health_report_ok(SYSTEM_MODULE_RTC,
                                       "RTC recovered") == ESP_OK,
               "RTC recovery report");
    TEST_CHECK(system_event_receive(&event, 0) == ESP_OK,
               "RTC CLEARED missing");
    TEST_CHECK((event.transition == SYSTEM_EVENT_CLEARED) &&
               (event.occurrence_count == SYSTEM_RECOVERY_FAST_RETRIES),
               "RTC clear mismatch");
    TEST_CHECK(system_health_get_snapshot(SYSTEM_MODULE_RTC, &health) == ESP_OK,
               "get recovered RTC snapshot");
    TEST_CHECK(health.state == SYSTEM_STATE_OK, "RTC must return to OK");

    system_time_mark_invalid("test invalid time");
    TEST_CHECK(!system_time_is_valid(), "invalid time accepted");
    TEST_CHECK(system_event_receive(&event, 0) == ESP_OK,
               "invalid time event missing");
    TEST_CHECK(!event.time_valid && (event.timestamp_utc_s == 0),
               "invalid time event invented a timestamp");
    const struct timeval tv = {
        .tv_sec = (time_t)SYSTEM_TIME_VALID_MIN_EPOCH + 86400,
        .tv_usec = 0,
    };
    TEST_CHECK(settimeofday(&tv, NULL) == 0, "set test clock");
    TEST_CHECK(system_time_mark_valid(SYSTEM_TIME_SOURCE_REMOTE,
                                      (int64_t)tv.tv_sec) == ESP_OK,
               "valid time rejected");
    TEST_CHECK(system_time_is_valid(), "TIME_VALID false");
    drain_events();
    TEST_CHECK(system_health_report_occurrence(
                   SYSTEM_MODULE_TIME, SYSTEM_EVENT_TIME_SYNCED,
                   SYSTEM_SEVERITY_INFO, ESP_OK, "timestamp test") == ESP_OK,
               "time occurrence");
    TEST_CHECK(system_event_receive(&event, 0) == ESP_OK,
               "timestamp event missing");
    TEST_CHECK(event.time_valid && (event.timestamp_utc_s >= tv.tv_sec),
               "event timestamp invalid");

    drain_events();
    const uint32_t dropped_before = system_event_get_dropped_count();
    for (unsigned i = 0U; i < SYSTEM_EVENT_QUEUE_LEN; ++i) {
        (void)system_health_report_occurrence(
            SYSTEM_MODULE_UI, SYSTEM_EVENT_MODULE_READY,
            SYSTEM_SEVERITY_INFO, ESP_OK, "fill queue");
    }
    (void)system_health_report_occurrence(
        SYSTEM_MODULE_UI, SYSTEM_EVENT_MODULE_READY,
        SYSTEM_SEVERITY_INFO, ESP_OK, "overflow info");
    (void)system_health_report_occurrence(
        SYSTEM_MODULE_SD, SYSTEM_EVENT_SD_LOW_SPACE,
        SYSTEM_SEVERITY_WARNING, ESP_ERR_NO_MEM, "priority warning");
    (void)system_health_report_occurrence(
        SYSTEM_MODULE_SYSTEM, SYSTEM_EVENT_TASK_STALL,
        SYSTEM_SEVERITY_CRITICAL, ESP_ERR_TIMEOUT, "priority event");
    TEST_CHECK(system_event_get_dropped_count() >= (dropped_before + 3U),
               "overflow counter not updated");

    bool found_warning = false;
    bool found_critical = false;
    bool found_overflow = false;
    while (system_event_receive(&event, 0) == ESP_OK) {
        found_warning |= (event.code == SYSTEM_EVENT_SD_LOW_SPACE);
        found_critical |= (event.code == SYSTEM_EVENT_TASK_STALL);
        found_overflow |= (event.code == SYSTEM_EVENT_QUEUE_OVERFLOW);
    }
    TEST_CHECK(found_warning, "warning event did not replace INFO");
    TEST_CHECK(found_critical, "critical event did not replace INFO");
    TEST_CHECK(found_overflow, "overflow notice missing");

    producer_ctx_t ctx = {
        .parent = xTaskGetCurrentTaskHandle(),
        .iterations = 25U,
    };
    TEST_CHECK(xTaskCreate(concurrent_producer, "contract_p1", 2048,
                           &ctx, 2, NULL) == pdPASS,
               "create producer 1");
    TEST_CHECK(xTaskCreate(concurrent_producer, "contract_p2", 2048,
                           &ctx, 2, NULL) == pdPASS,
               "create producer 2");
    TEST_CHECK(ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(5000)) > 0U,
               "producer 1 timeout");
    TEST_CHECK(ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(5000)) > 0U,
               "producer 2 timeout");
    TEST_CHECK(system_health_get_snapshot(SYSTEM_MODULE_NVS, &health) == ESP_OK,
               "get NVS snapshot");
    TEST_CHECK((health.total_failures == 50U) &&
               (health.active_occurrences == 50U),
               "concurrent counters corrupted");

    TEST_CHECK(data_serializer_init() == ESP_OK, "serializer init");
    const MeterData_t meter = {.vrms = 120.0f, .irms_a = 1.0f};
    const system_record_base_t record = {.boot_id = UINT32_MAX, .sequence = 42,
        .uptime_ms = INT64_C(99999999), .time_valid = false};
    static char packet[STORAGE_SERVICE_PAYLOAD_MAX];
    static char replay[STORAGE_SERVICE_PAYLOAD_MAX];
    TEST_CHECK(data_serializer_build_senml_datos_record(&meter, &record,
        packet, sizeof(packet)) == ESP_OK, "trace packet truncated");
    TEST_CHECK(data_serializer_build_senml_datos_record(&meter, &record,
        replay, sizeof(replay)) == ESP_OK && strcmp(packet, replay) == 0,
        "replay changed measurement identity");
    TEST_CHECK(strstr(packet, "\"n\":\"boot_id\",\"v\":4294967295") &&
        strstr(packet, "\"n\":\"seq\",\"v\":42") &&
        strstr(packet, "\"n\":\"time_valid\",\"vb\":false") &&
        strstr(packet, "\"n\":\"fw\",\"vs\":"), "trace fields missing");
    (void)node_health_init();
    for (unsigned i = 0; i < 3; ++i) { node_health_mqtt_success_inc(); }
    for (unsigned i = 0; i < 2; ++i) { node_health_msg_tx_inc(); }
    TEST_CHECK(data_serializer_build_senml_estado(&meter, false, false, false,
        packet, sizeof(packet)) == ESP_OK, "status trace counters truncated");
    TEST_CHECK(strstr(packet, "\"n\":\"mqtt_sessions_ok\",\"v\":3") &&
        strstr(packet, "\"n\":\"data_publish_ok\",\"v\":2") &&
        strstr(packet, "\"n\":\"reconnect_count\",\"v\":2"),
        "status confused sessions with published data");
    LOG_INFO(TAG, "PASS states dedup clear time calendar overflow priority concurrency CPSI trace");
    return true;
}
