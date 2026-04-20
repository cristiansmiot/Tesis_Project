#include "active_power.h"

#include <stdbool.h>
#include <stdint.h>
#include "ade9153a_regs.h"
#include "ade9153a_spi.h"
#include "conversion_constants.h"
#include "meter_config.h"
#include "logger.h"

static const char *TAG = "active_power";
static float s_target_wcc;
static float s_target_whcc;
static bool s_initialized;
static bool s_warned_w_unverified;
static bool s_warned_wh_unverified;
static bool s_warned_wh_lo_read_failed;

/**
 * @brief Inicializa constantes del modulo de potencia activa.
 * @param target_wcc Constante [uW/code].
 * @param target_whcc Constante [nWh/code].
 * @return ESP_OK en caso de exito.
 */
esp_err_t active_power_init(float target_wcc, float target_whcc)
{
    if ((target_wcc <= 0.0f) || (target_whcc <= 0.0f)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_target_wcc = target_wcc;
    s_target_whcc = target_whcc;
    s_initialized = true;
    s_warned_w_unverified = false;
    s_warned_wh_unverified = false;
    s_warned_wh_lo_read_failed = false;
    return ESP_OK;
}

/**
 * @brief Lee AWATT y lo convierte a watts.
 * @param void Sin parametros.
 * @return Potencia activa en watts.
 */
float active_power_get_watts(void)
{
    if (!s_initialized) {
        LOG_WARN(TAG, "active_power module not initialized");
        return 0.0f;
    }

#if !METER_VHEADROOM_VERIFIED && !METER_USE_ADI_API_CC_DEFAULTS
    if (!s_warned_w_unverified) {
        LOG_WARN(TAG, "TARGET_WCC unverified and ADI defaults disabled - returning 0.0f");
        s_warned_w_unverified = true;
    }
    return 0.0f;
#else
    uint32_t raw_u32 = 0U;
    if (ade9153a_read_reg32(ADE9153A_REG_AWATT, &raw_u32) != ESP_OK) {
        LOG_WARN(TAG, "AWATT read failed");
        return 0.0f;
    }
    const int32_t raw_s32 = (int32_t)raw_u32;  // Registro signed segun datasheet.

    // AWATT [W] = crudo * TARGET_WCC[uW/code] * 1e-6
    return METER_POWER_SIGN_CORRECTION * ((float)raw_s32) * s_target_wcc * 1e-6f;
#endif
}

/**
 * @brief Convierte AWATTHR_HI a Wh (ruta de compatibilidad ADI API).
 * @param raw_hi Registro AWATTHR_HI signed.
 * @return Energia en Wh.
 */
static float active_power_convert_wh_from_hi(int32_t raw_hi)
{
    return METER_POWER_SIGN_CORRECTION * ((float)raw_hi) * s_target_whcc * 1e-9f;
}

/**
 * @brief Convierte AWATTHR_HI:AWATTHR_LO (Q32.32) a Wh para mayor resolucion.
 * @param raw_hi MSBs signed.
 * @param raw_lo LSBs unsigned.
 * @return Energia en Wh.
 */
static float active_power_convert_wh_from_hilo(int32_t raw_hi, uint32_t raw_lo)
{
    const int64_t raw_q32_32 = (((int64_t)raw_hi) << 32) | ((int64_t)raw_lo);
    const double raw_codes = (double)raw_q32_32 / 4294967296.0;  // 2^32
    const double wh_per_code = ((double)s_target_whcc) * 1e-9;
    const double wh = ((double)METER_POWER_SIGN_CORRECTION) * raw_codes * wh_per_code;
    return (float)wh;
}

/**
 * @brief Lee AWATTHR_HI y lo convierte a Wh.
 * @param void Sin parametros.
 * @return Energia activa en Wh.
 */
float active_power_get_wh(void)
{
    if (!s_initialized) {
        LOG_WARN(TAG, "active_power module not initialized");
        return 0.0f;
    }

#if !METER_ENERGY_REGS_VERIFIED
    if (!s_warned_wh_unverified) {
        LOG_WARN(TAG, "energy registers unverified - returning 0.0f");
        s_warned_wh_unverified = true;
    }
    return 0.0f;
#else
    uint32_t raw_hi_u32 = 0U;
    if (ade9153a_read_reg32(ADE9153A_REG_AWATTHR_HI, &raw_hi_u32) != ESP_OK) {
        LOG_WARN(TAG, "AWATTHR_HI read failed");
        return 0.0f;
    }
    const int32_t raw_hi_s32 = (int32_t)raw_hi_u32;  // Acumulador de energia signed (MSBs).

    uint32_t raw_lo_u32 = 0U;
    if (ade9153a_read_reg32(ADE9153A_REG_AWATTHR_LO, &raw_lo_u32) != ESP_OK) {
        if (!s_warned_wh_lo_read_failed) {
            LOG_WARN(TAG, "AWATTHR_LO read failed - fallback to HI only");
            s_warned_wh_lo_read_failed = true;
        }
        return active_power_convert_wh_from_hi(raw_hi_s32);
    }

    return active_power_convert_wh_from_hilo(raw_hi_s32, raw_lo_u32);
#endif
}

/**
 * @brief Lee AWATTHR_HI crudo para diagnostico.
 * @param void Sin parametros.
 * @return Valor signed del registro o 0 ante error.
 */
int32_t active_power_get_wh_raw_hi(void)
{
    if (!s_initialized) {
        LOG_WARN(TAG, "active_power module not initialized");
        return 0;
    }

#if !METER_ENERGY_REGS_VERIFIED
    return 0;
#else
    uint32_t raw_hi_u32 = 0U;
    if (ade9153a_read_reg32(ADE9153A_REG_AWATTHR_HI, &raw_hi_u32) != ESP_OK) {
        return 0;
    }
    return (int32_t)raw_hi_u32;
#endif
}
