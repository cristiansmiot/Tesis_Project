#include "task_calibration.h"

#include <string.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "meter_config.h"
#include "meter_data.h"
#include "msure.h"
#include "nvm_calib.h"
#include "rtos_app_config.h"
#include "task_manager.h"
#include "logger.h"

static const char *TAG = "task_calibration";
static TaskHandle_t s_task_handle;
static volatile uint8_t s_calib_status;

static void task_calibration_set_status(uint8_t status)
{
    s_calib_status = status;
}

uint8_t task_calibration_get_status(void)
{
    return s_calib_status;
}

static bool task_calibration_record_is_valid(const nvm_calib_record_t *record)
{
    if (record == NULL) {
        return false;
    }

    msure_result_t tmp = {
        .aicert_raw = record->aicert_raw,
        .avcert_raw = record->avcert_raw,
        .aicc_nA_per_code = record->aicc_nA_per_code,
        .avcc_nV_per_code = record->avcc_nV_per_code
    };

    return msure_result_is_plausible(&tmp) && msure_cert_is_valid(&tmp);
}

static bool task_calibration_wait_ac_stable(uint32_t timeout_ms)
{
    uint32_t elapsed_ms = 0U;
    uint32_t stable_samples = 0U;

    while (elapsed_ms < timeout_ms) {
        const MeterData_t snap = meter_data_get_snapshot();
        const bool freq_ok = (snap.frequency >= METER_AC_PRESENT_FREQ_MIN_HZ) &&
                             (snap.frequency <= METER_AC_PRESENT_FREQ_MAX_HZ);
        const bool vrms_ok = (snap.vrms >= METER_AC_PRESENT_VRMS_MIN_V) &&
                             (snap.vrms <= METER_VRMS_SANITY_MAX_V);

        if (freq_ok && vrms_ok) {
            ++stable_samples;
            if (stable_samples >= METER_MSURE_BOOT_STABLE_SAMPLES) {
                return true;
            }
        } else {
            stable_samples = 0U;
        }

        vTaskDelay(pdMS_TO_TICKS(METER_MEASUREMENT_PERIOD_MS));
        elapsed_ms += METER_MEASUREMENT_PERIOD_MS;
    }
    return false;
}

static bool task_calibration_apply_persisted(void)
{
    nvm_calib_record_t record = {0};
    const esp_err_t err = nvm_calib_load(&record);
    if (err != ESP_OK) {
        LOG_INFO(TAG, "no calibration record in NVS (%s)", esp_err_to_name(err));
        return false;
    }
    if (!task_calibration_record_is_valid(&record)) {
        LOG_WARN(TAG, "persisted calibration record invalid - skipping apply");
        return false;
    }
    if (nvm_calib_apply(&record) != ESP_OK) {
        LOG_WARN(TAG, "persisted calibration apply failed");
        task_calibration_set_status(CALIB_STATUS_ERROR);
        return false;
    }

    uint8_t status = (uint8_t)(CALIB_STATUS_LOADED | CALIB_STATUS_PERSISTED);
    msure_result_t tmp = {
        .aicert_raw = record.aicert_raw,
        .avcert_raw = record.avcert_raw
    };
    if (msure_cert_is_valid(&tmp)) {
        status = (uint8_t)(status | CALIB_STATUS_VALID);
    }
    task_calibration_set_status(status);

    LOG_INFO(TAG,
             "calibration loaded from NVS: mode=%u AIGAIN=%ld AVGAIN=%ld AICERT=%ld AVCERT=%ld",
             (unsigned)record.mode,
             (long)record.aigain,
             (long)record.avgain,
             (long)record.aicert_raw,
             (long)record.avcert_raw);
    return true;
}

static void task_calibration_process_request(const CalibRequest_t *req)
{
    if (req == NULL) {
        return;
    }

    const bool run_channel_a =
        (req->request_type == CALIB_REQUEST_FULL) ||
        (req->request_type == CALIB_REQUEST_CHANNEL_A) ||
        (req->request_type == CALIB_REQUEST_CHANNEL_A_TURBO);
    const bool run_channel_v = (req->request_type == CALIB_REQUEST_FULL);
    if (!run_channel_a && !run_channel_v) {
        LOG_WARN(TAG, "unsupported calibration request=%u", (unsigned)req->request_type);
        task_calibration_set_status(CALIB_STATUS_ERROR);
        return;
    }

    const msure_mode_t mode =
        (req->request_type == CALIB_REQUEST_CHANNEL_A_TURBO) ?
        MSURE_MODE_AI_TURBO :
        (METER_MSURE_USE_TURBO ? MSURE_MODE_AI_TURBO : MSURE_MODE_AI_NORMAL);

    uint8_t status = CALIB_STATUS_RUNNING;
    task_calibration_set_status(status);

    msure_result_t result = {0};
    const esp_err_t run_err = msure_run(run_channel_a, run_channel_v, mode, &result);
    if (run_err != ESP_OK) {
        LOG_WARN(TAG, "msure_run failed: %s", esp_err_to_name(run_err));
        task_calibration_set_status((uint8_t)(CALIB_STATUS_ERROR));
        return;
    }

    const bool cert_ok = msure_cert_is_valid(&result);
    if (cert_ok) {
        status = (uint8_t)(status | CALIB_STATUS_VALID);
    } else {
        LOG_WARN(TAG,
                 "mSure certainty above threshold: AICERT=%ld AVCERT=%ld lim=%d",
                 (long)result.aicert_raw,
                 (long)result.avcert_raw,
                 (int)METER_MSURE_CERT_PPM);
    }

    nvm_calib_record_t record = {0};
    record.timestamp_us = esp_timer_get_time();
    record.aicc_raw = result.aicc_raw;
    record.aicert_raw = result.aicert_raw;
    record.avcc_raw = result.avcc_raw;
    record.avcert_raw = result.avcert_raw;
    record.aicc_nA_per_code = result.aicc_nA_per_code;
    record.avcc_nV_per_code = result.avcc_nV_per_code;
    record.aigain = result.aigain;
    record.avgain = result.avgain;
    record.mode = (uint8_t)result.mode;

    if (nvm_calib_save(&record) == ESP_OK) {
        status = (uint8_t)(status | CALIB_STATUS_PERSISTED);
    } else {
        LOG_WARN(TAG, "nvm_calib_save failed");
        status = (uint8_t)(status | CALIB_STATUS_ERROR);
    }

    status = (uint8_t)(status | CALIB_STATUS_LOADED);
    status = (uint8_t)(status & ((uint8_t)~CALIB_STATUS_RUNNING));
    task_calibration_set_status(status);
}

static void task_calibration(void *pvParameters)
{
    (void)pvParameters;

    task_calibration_set_status(0U);

    if (msure_init() != ESP_OK) {
        LOG_ERROR(TAG, "msure_init failed");
        task_calibration_set_status(CALIB_STATUS_ERROR);
    }
    bool loaded_from_nvs = false;
    if (nvm_calib_init() != ESP_OK) {
        LOG_ERROR(TAG, "nvm_calib_init failed");
        task_calibration_set_status(CALIB_STATUS_ERROR);
    } else if (METER_MSURE_REAPPLY_ON_BOOT) {
        loaded_from_nvs = task_calibration_apply_persisted();
    }

    if (!loaded_from_nvs) {
        if (METER_MSURE_AUTORUN_ON_BOOT) {
            LOG_INFO(TAG, "no persisted calibration; waiting AC stable for boot mSure");
            if (task_calibration_wait_ac_stable(METER_MSURE_BOOT_WAIT_AC_TIMEOUT_MS)) {
                CalibRequest_t boot_req = {.request_type = CALIB_REQUEST_FULL};
                LOG_INFO(TAG, "AC stable; running boot mSure calibration");
                task_calibration_process_request(&boot_req);
            } else {
                LOG_WARN(TAG, "boot mSure skipped: AC not stable within timeout");
            }
        } else {
            LOG_INFO(TAG, "no persisted calibration; boot mSure disabled by config");
        }
    }

    for (;;) {
        CalibRequest_t req = {0};
        if (q_calib_request == NULL) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        const BaseType_t rc = xQueueReceive(q_calib_request, &req, pdMS_TO_TICKS(1000));
        if (rc == pdTRUE) {
            task_calibration_process_request(&req);
        }
    }
}

/**
 * @brief Crea y arranca task_calibration.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t task_calibration_start(void)
{
    if (s_task_handle != NULL) {
        return ESP_OK;
    }

    const BaseType_t rc = xTaskCreatePinnedToCore(
        task_calibration,
        "task_calibration",
        TASK_STACK_CALIBRATION,
        NULL,
        TASK_PRIO_CALIBRATION,
        &s_task_handle,
        1
    );
    if (rc != pdPASS) {
        LOG_ERROR(TAG, "task_calibration create failed");
        s_task_handle = NULL;
        return ESP_FAIL;
    }

    LOG_INFO(TAG, "task_calibration started on core 1");
    return ESP_OK;
}

