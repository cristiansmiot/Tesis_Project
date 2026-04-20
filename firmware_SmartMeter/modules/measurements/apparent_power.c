#include "apparent_power.h"

#include <stdbool.h>
#include <stdint.h>
#include "ade9153a_regs.h"
#include "ade9153a_spi.h"
#include "conversion_constants.h"
#include "logger.h"

static const char *TAG = "apparent_power";
static float s_target_vacc;
static float s_target_vahcc;
static bool s_initialized;
static bool s_warned_va_unverified;
static bool s_warned_vah_unverified;

/**
 * @brief Inicializa constantes del modulo aparente.
 * @param target_vacc Constante [uVA/code].
 * @param target_vahcc Constante [nVAh/code].
 * @return ESP_OK en caso de exito.
 */
esp_err_t apparent_power_init(float target_vacc, float target_vahcc)
{
    if ((target_vacc <= 0.0f) || (target_vahcc <= 0.0f)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_target_vacc = target_vacc;
    s_target_vahcc = target_vahcc;
    s_initialized = true;
    s_warned_va_unverified = false;
    s_warned_vah_unverified = false;
    return ESP_OK;
}

/**
 * @brief Lee AVA y lo convierte a VA.
 * @param void Sin parametros.
 * @return Potencia aparente en VA.
 */
float apparent_power_get_va(void)
{
    if (!s_initialized) {
        LOG_WARN(TAG, "apparent_power module not initialized");
        return 0.0f;
    }

#if !METER_VHEADROOM_VERIFIED && !METER_USE_ADI_API_CC_DEFAULTS
    if (!s_warned_va_unverified) {
        LOG_WARN(TAG, "TARGET_VACC unverified and ADI defaults disabled - returning 0.0f");
        s_warned_va_unverified = true;
    }
    return 0.0f;
#else
    uint32_t raw_u32 = 0U;
    if (ade9153a_read_reg32(ADE9153A_REG_AVA, &raw_u32) != ESP_OK) {
        LOG_WARN(TAG, "AVA read failed");
        return 0.0f;
    }

    // AVA [VA] = crudo * TARGET_VACC[uVA/code] * 1e-6
    return ((float)raw_u32) * s_target_vacc * 1e-6f;
#endif
}

/**
 * @brief Lee AVAHR_HI y lo convierte a VAh.
 * @param void Sin parametros.
 * @return Energia aparente en VAh.
 */
float apparent_power_get_vah(void)
{
    if (!s_initialized) {
        LOG_WARN(TAG, "apparent_power module not initialized");
        return 0.0f;
    }

#if !METER_ENERGY_REGS_VERIFIED
    if (!s_warned_vah_unverified) {
        LOG_WARN(TAG, "energy registers unverified - returning 0.0f");
        s_warned_vah_unverified = true;
    }
    return 0.0f;
#else
    uint32_t raw_u32 = 0U;
    if (ade9153a_read_reg32(ADE9153A_REG_AVAHR_HI, &raw_u32) != ESP_OK) {
        LOG_WARN(TAG, "AVAHR_HI read failed");
        return 0.0f;
    }

    // AVAHR_HI [VAh] = crudo * TARGET_VAHCC[nVAh/code] * 1e-9
    return ((float)raw_u32) * s_target_vahcc * 1e-9f;
#endif
}
