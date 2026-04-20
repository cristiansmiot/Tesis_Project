#include "reactive_power.h"

#include <stdbool.h>
#include <stdint.h>
#include "ade9153a_regs.h"
#include "ade9153a_spi.h"
#include "conversion_constants.h"
#include "meter_config.h"
#include "logger.h"

static const char *TAG = "reactive_power";
static float s_target_varcc;
static float s_target_varhcc;
static bool s_initialized;
static bool s_warned_var_unverified;
static bool s_warned_varh_unverified;

/**
 * @brief Inicializa constantes del modulo reactivo.
 * @param target_varcc Constante [uVAR/code].
 * @param target_varhcc Constante [nVARh/code].
 * @return ESP_OK en caso de exito.
 */
esp_err_t reactive_power_init(float target_varcc, float target_varhcc)
{
    if ((target_varcc <= 0.0f) || (target_varhcc <= 0.0f)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_target_varcc = target_varcc;
    s_target_varhcc = target_varhcc;
    s_initialized = true;
    s_warned_var_unverified = false;
    s_warned_varh_unverified = false;
    return ESP_OK;
}

/**
 * @brief Lee AFVAR y lo convierte a VAR.
 * @param void Sin parametros.
 * @return Potencia reactiva en VAR.
 */
float reactive_power_get_var(void)
{
    if (!s_initialized) {
        LOG_WARN(TAG, "reactive_power module not initialized");
        return 0.0f;
    }

#if !METER_VHEADROOM_VERIFIED && !METER_USE_ADI_API_CC_DEFAULTS
    if (!s_warned_var_unverified) {
        LOG_WARN(TAG, "TARGET_VARCC unverified and ADI defaults disabled - returning 0.0f");
        s_warned_var_unverified = true;
    }
    return 0.0f;
#else
    uint32_t raw_u32 = 0U;
    if (ade9153a_read_reg32(ADE9153A_REG_AFVAR, &raw_u32) != ESP_OK) {
        LOG_WARN(TAG, "AFVAR read failed");
        return 0.0f;
    }
    const int32_t raw_s32 = (int32_t)raw_u32;  // Registro signed segun datasheet.

    // AFVAR [VAR] = crudo * TARGET_VARCC[uVAR/code] * 1e-6
    return METER_POWER_SIGN_CORRECTION * ((float)raw_s32) * s_target_varcc * 1e-6f;
#endif
}

/**
 * @brief Lee AFVARHR_HI y lo convierte a VARh.
 * @param void Sin parametros.
 * @return Energia reactiva en VARh.
 */
float reactive_power_get_varh(void)
{
    if (!s_initialized) {
        LOG_WARN(TAG, "reactive_power module not initialized");
        return 0.0f;
    }

#if !METER_ENERGY_REGS_VERIFIED
    if (!s_warned_varh_unverified) {
        LOG_WARN(TAG, "energy registers unverified - returning 0.0f");
        s_warned_varh_unverified = true;
    }
    return 0.0f;
#else
    uint32_t raw_u32 = 0U;
    if (ade9153a_read_reg32(ADE9153A_REG_AFVARHR_HI, &raw_u32) != ESP_OK) {
        LOG_WARN(TAG, "AFVARHR_HI read failed");
        return 0.0f;
    }
    const int32_t raw_s32 = (int32_t)raw_u32;  // Acumulador de energia signed (MSBs).

    // AFVARHR_HI [VARh] = crudo * TARGET_VARHCC[nVARh/code] * 1e-9
    return METER_POWER_SIGN_CORRECTION * ((float)raw_s32) * s_target_varhcc * 1e-9f;
#endif
}
