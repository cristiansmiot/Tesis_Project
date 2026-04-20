#include "current.h"

#include <stdbool.h>
#include <stdint.h>
#include "ade9153a_regs.h"
#include "ade9153a_spi.h"
#include "meter_config.h"
#include "logger.h"

static const char *TAG = "current";
static float s_target_aicc;
static float s_target_bicc;
static bool s_initialized;
static bool s_warned_irms_a_range;
static bool s_warned_irms_b_range;

/**
 * @brief Inicializa constantes del modulo de corriente.
 * @param target_aicc Constante [nA/code] canal A.
 * @param target_bicc Constante [nA/code] canal B.
 * @return ESP_OK en caso de exito.
 */
esp_err_t current_init(float target_aicc, float target_bicc)
{
    if ((target_aicc <= 0.0f) || (target_bicc <= 0.0f)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_target_aicc = target_aicc;
    s_target_bicc = target_bicc;
    s_initialized = true;
    s_warned_irms_a_range = false;
    s_warned_irms_b_range = false;
    return ESP_OK;
}

/**
 * @brief Lee AIRMS y lo convierte a amperes.
 * @param void Sin parametros.
 * @return Corriente RMS A.
 */
float current_get_irms_a(void)
{
    if (!s_initialized) {
        LOG_WARN(TAG, "current module not initialized");
        return 0.0f;
    }

    uint32_t raw_u32 = 0U;
    if (ade9153a_read_reg32(ADE9153A_REG_AIRMS, &raw_u32) != ESP_OK) {
        LOG_WARN(TAG, "AIRMS read failed");
        return 0.0f;
    }

    // AIRMS [A] = crudo * TARGET_AICC[nA/code] * 1e-9
    const float irms_a = ((float)raw_u32) * s_target_aicc * 1e-9f;
    if (irms_a > (METER_IMAX_A * 2.0f)) {
        if (!s_warned_irms_a_range) {
            LOG_WARN(TAG, "IRMS_A out of expected range - forcing 0.0f");
            s_warned_irms_a_range = true;
        }
        return 0.0f;
    }
    return irms_a;
}

/**
 * @brief Lee BIRMS y lo convierte a amperes.
 * @param void Sin parametros.
 * @return Corriente RMS B.
 */
float current_get_irms_b(void)
{
    if (!s_initialized) {
        LOG_WARN(TAG, "current module not initialized");
        return 0.0f;
    }

    if (METER_BI_PGAGAIN == 1) {
        return 0.0f;
    }

    uint32_t raw_u32 = 0U;
    if (ade9153a_read_reg32(ADE9153A_REG_BIRMS, &raw_u32) != ESP_OK) {
        LOG_WARN(TAG, "BIRMS read failed");
        return 0.0f;
    }

    // BIRMS [A] = crudo * TARGET_BICC[nA/code] * 1e-9
    const float irms_b = ((float)raw_u32) * s_target_bicc * 1e-9f;
    if (irms_b > (METER_IMAX_A * 2.0f)) {
        if (!s_warned_irms_b_range) {
            LOG_WARN(TAG, "IRMS_B out of expected range - forcing 0.0f");
            s_warned_irms_b_range = true;
        }
        return 0.0f;
    }
    return irms_b;
}
