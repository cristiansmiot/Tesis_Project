#include "msure.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "ade9153a_regs.h"
#include "ade9153a_spi.h"
#include "conversion_constants.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "meter_config.h"
#include "logger.h"

static const char *TAG = "msure";
static bool s_initialized;

static int32_t msure_to_gain(float measured_cc, float target_cc)
{
    if (target_cc <= 0.0f) {
        return 0;
    }
    const double ratio = (double)measured_cc / (double)target_cc;
    const double gain = (ratio - 1.0) * 134217728.0;  // 2^27
    if (gain > (double)INT32_MAX) {
        return INT32_MAX;
    }
    if (gain < (double)INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)lrint(gain);
}

static void msure_delay_ms(uint32_t ms)
{
    if (ms == 0U) {
        return;
    }
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        esp_rom_delay_us(ms * 1000U);
        return;
    }
    const TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay((ticks > 0U) ? ticks : 1U);
}

static esp_err_t msure_wait_ready(void)
{
    const uint32_t max_loops = (METER_MSURE_READY_TIMEOUT_MS / 100U) + 1U;
    for (uint32_t i = 0U; i < max_loops; ++i) {
        uint32_t status = 0U;
        const esp_err_t err = ade9153a_read_reg32(ADE9153A_REG_MS_STATUS_CURRENT, &status);
        if (err != ESP_OK) {
            return err;
        }
        if ((status & 0x00000001U) != 0U) {
            return ESP_OK;
        }
        msure_delay_ms(100U);
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t msure_start_and_wait(uint32_t start_cmd)
{
    esp_err_t err = msure_wait_ready();
    if (err != ESP_OK) {
        LOG_WARN(TAG, "mSure wait-ready failed: %s", esp_err_to_name(err));
        return err;
    }

    err = ade9153a_write_reg32(ADE9153A_REG_MS_ACAL_CFG, start_cmd);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "MS_ACAL_CFG start write failed");
        return err;
    }

    msure_delay_ms((uint32_t)METER_MSURE_INIT_WAIT_MS);

    err = ade9153a_write_reg32(ADE9153A_REG_MS_ACAL_CFG, ADE9153A_MSURE_STOP);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "MS_ACAL_CFG stop write failed");
        return err;
    }
    return ESP_OK;
}

static esp_err_t msure_read_result(msure_result_t *out)
{
    uint32_t raw_u32 = 0U;
    if (ade9153a_read_reg32(ADE9153A_REG_MS_ACAL_AICC, &raw_u32) != ESP_OK) {
        return ESP_FAIL;
    }
    out->aicc_raw = (int32_t)raw_u32;

    if (ade9153a_read_reg32(ADE9153A_REG_MS_ACAL_AICERT, &raw_u32) != ESP_OK) {
        return ESP_FAIL;
    }
    out->aicert_raw = (int32_t)raw_u32;

    if (ade9153a_read_reg32(ADE9153A_REG_MS_ACAL_AVCC, &raw_u32) != ESP_OK) {
        return ESP_FAIL;
    }
    out->avcc_raw = (int32_t)raw_u32;

    if (ade9153a_read_reg32(ADE9153A_REG_MS_ACAL_AVCERT, &raw_u32) != ESP_OK) {
        return ESP_FAIL;
    }
    out->avcert_raw = (int32_t)raw_u32;

    out->aicc_nA_per_code = ((float)out->aicc_raw) / 2048.0f;
    out->avcc_nV_per_code = ((float)out->avcc_raw) / 2048.0f;
    out->aigain = msure_to_gain(out->aicc_nA_per_code, TARGET_AICC);
    out->avgain = msure_to_gain(out->avcc_nV_per_code, TARGET_AVCC);
    return ESP_OK;
}

static esp_err_t msure_apply_gains(const msure_result_t *result)
{
    esp_err_t err = ade9153a_write_reg32(ADE9153A_REG_AIGAIN, (uint32_t)result->aigain);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "AIGAIN write failed");
        return err;
    }
    err = ade9153a_write_reg32(ADE9153A_REG_AVGAIN, (uint32_t)result->avgain);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "AVGAIN write failed");
        return err;
    }
    return ESP_OK;
}

/**
 * @brief Inicializa el modulo mSure.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t msure_init(void)
{
    s_initialized = true;
    return ESP_OK;
}

esp_err_t msure_run(bool run_a, bool run_v, msure_mode_t mode, msure_result_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!run_a && !run_v) {
        return ESP_ERR_INVALID_ARG;
    }

    (void)memset(out, 0, sizeof(*out));
    out->mode = mode;

    if (run_a) {
        const uint32_t cmd = (mode == MSURE_MODE_AI_TURBO) ?
                             ADE9153A_MSURE_START_A_TURBO :
                             ADE9153A_MSURE_START_A;
        esp_err_t err = msure_start_and_wait(cmd);
        if (err != ESP_OK) {
            return err;
        }
    }
    if (run_v) {
        esp_err_t err = msure_start_and_wait(ADE9153A_MSURE_START_V);
        if (err != ESP_OK) {
            return err;
        }
    }

    esp_err_t err = msure_read_result(out);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "mSure result read failed");
        return err;
    }

    if (!msure_result_is_plausible(out)) {
        LOG_WARN(TAG,
                 "mSure result out of plausible range: AICC=%.3f AVCC=%.3f",
                 (double)out->aicc_nA_per_code,
                 (double)out->avcc_nV_per_code);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (!msure_cert_is_valid(out)) {
        LOG_WARN(TAG,
                 "mSure certainty invalid: AICERT=%ld AVCERT=%ld lim=%d",
                 (long)out->aicert_raw,
                 (long)out->avcert_raw,
                 (int)METER_MSURE_CERT_PPM);
        return ESP_ERR_INVALID_RESPONSE;
    }

    err = msure_apply_gains(out);
    if (err != ESP_OK) {
        return err;
    }

    LOG_INFO(TAG,
             "mSure done: mode=%s AICC=%.3f AICERT=%ld AVCC=%.3f AVCERT=%ld AIGAIN=%ld AVGAIN=%ld",
             (mode == MSURE_MODE_AI_TURBO) ? "turbo" : "normal",
             (double)out->aicc_nA_per_code,
             (long)out->aicert_raw,
             (double)out->avcc_nV_per_code,
             (long)out->avcert_raw,
             (long)out->aigain,
             (long)out->avgain);
    return ESP_OK;
}

bool msure_cert_is_valid(const msure_result_t *result)
{
    if (result == NULL) {
        return false;
    }
    if ((result->aicert_raw < 0) || (result->avcert_raw < 0)) {
        return false;
    }
    return (result->aicert_raw <= (int32_t)METER_MSURE_CERT_PPM) &&
           (result->avcert_raw <= (int32_t)METER_MSURE_CERT_PPM);
}

bool msure_result_is_plausible(const msure_result_t *result)
{
    if (result == NULL) {
        return false;
    }

    if (!isfinite(result->aicc_nA_per_code) || !isfinite(result->avcc_nV_per_code)) {
        return false;
    }

    const float aicc_min = TARGET_AICC * METER_MSURE_CC_MIN_RATIO;
    const float aicc_max = TARGET_AICC * METER_MSURE_CC_MAX_RATIO;
    const float avcc_min = TARGET_AVCC * METER_MSURE_CC_MIN_RATIO;
    const float avcc_max = TARGET_AVCC * METER_MSURE_CC_MAX_RATIO;

    if ((result->aicc_nA_per_code < aicc_min) || (result->aicc_nA_per_code > aicc_max)) {
        return false;
    }
    if ((result->avcc_nV_per_code < avcc_min) || (result->avcc_nV_per_code > avcc_max)) {
        return false;
    }

    return true;
}

