#include "task_time.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger.h"
#include "meter_config.h"
#include "rtos_app_config.h"
#include "system_health.h"
#include "task_monitor.h"
#include "time_service.h"

static const char *TAG = "task_time";
static TaskHandle_t s_task_handle;

static TickType_t retry_delay(uint32_t failures)
{
    if (failures == 1U) {
        return pdMS_TO_TICKS(1000U);
    }
    if (failures == 2U) {
        return pdMS_TO_TICKS(2000U);
    }
    if (failures == 3U) {
        return pdMS_TO_TICKS(4000U);
    }
    return pdMS_TO_TICKS(METER_RTC_RECOVERY_SLOW_MS);
}

static void task_time(void *pv_parameters)
{
    (void)pv_parameters;
    uint32_t failures = 0U;
    TickType_t next_rtc_check = 0U;

    LOG_INFO(TAG, "task_time ejecutando");
    (void)system_health_mark_recovering(SYSTEM_MODULE_RTC,
                                        "RTC startup probe");
    for (;;) {
        task_monitor_heartbeat(TASK_MON_TIME);

        while (time_service_process_pending_sync() == ESP_OK) {
            (void)system_health_report_ok(SYSTEM_MODULE_TIME,
                                          "authoritative UTC applied");
        }

        const TickType_t now = xTaskGetTickCount();
        if ((next_rtc_check == 0U) || ((int32_t)(now - next_rtc_check) >= 0)) {
            if (failures > 0U) {
                (void)system_health_mark_recovering(SYSTEM_MODULE_RTC,
                                                    "RTC recovery probe");
            }
            const esp_err_t err = (next_rtc_check == 0U)
                ? time_service_apply_rtc_bootstrap()
                : time_service_poll_rtc();

            if (next_rtc_check == 0U) {
                LOG_INFO(TAG, "RTC bootstrap finalizo: %s",
                         esp_err_to_name(err));
            }

            time_service_snapshot_t snap = {0};
            (void)time_service_get_snapshot(&snap);
            if (err == ESP_OK) {
                failures = 0U;
                (void)system_health_report_ok(SYSTEM_MODULE_RTC,
                                              "RTC healthy and time valid");
                next_rtc_check = now + pdMS_TO_TICKS(METER_RTC_POLL_MS);
            } else if ((err == ESP_ERR_INVALID_STATE) && snap.rtc_ready) {
                ++failures;
                (void)system_health_report_failure(
                    SYSTEM_MODULE_RTC,
                    snap.rtc_oscillator_stopped
                        ? SYSTEM_EVENT_RTC_BACKUP_LOST
                        : SYSTEM_EVENT_TIME_INVALID,
                    SYSTEM_SEVERITY_WARNING, err,
                    snap.rtc_oscillator_stopped
                        ? "RTC backup lost; check VBAT and synchronize"
                        : (snap.rtc_eosc_set
                            ? "RTC EOSC set; oscillator disabled on VBAT"
                            : "RTC calendar outside valid range"));
                if (!system_time_is_valid()) {
                    system_time_mark_invalid("RTC time invalid; waiting sync");
                }
                next_rtc_check = now + retry_delay(failures);
            } else {
                ++failures;
                (void)system_health_report_failure(
                    SYSTEM_MODULE_RTC, SYSTEM_EVENT_IO_ERROR,
                    SYSTEM_SEVERITY_WARNING, err,
                    "RTC communication failed");
                if (!system_time_is_valid()) {
                    system_time_mark_invalid("RTC unavailable; waiting sync");
                }
                next_rtc_check = now + retry_delay(failures);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(250U));
    }
}

esp_err_t task_time_start(void)
{
    if (s_task_handle != NULL) {
        return ESP_OK;
    }
    const esp_err_t init_err = time_service_init();
    if (init_err != ESP_OK) {
        return init_err;
    }
    const BaseType_t rc = xTaskCreatePinnedToCore(
        task_time, "task_time", TASK_STACK_TIME, NULL, TASK_PRIO_TIME,
        &s_task_handle, tskNO_AFFINITY);
    if (rc != pdPASS) {
        s_task_handle = NULL;
        LOG_ERROR(TAG, "no se pudo crear task_time");
        return ESP_FAIL;
    }
    LOG_INFO(TAG, "task_time iniciada");
    return ESP_OK;
}
