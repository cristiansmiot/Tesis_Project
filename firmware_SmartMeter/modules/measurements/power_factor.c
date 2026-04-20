#include "power_factor.h"

#include <stdbool.h>
#include <stdint.h>
#include "ade9153a_regs.h"
#include "ade9153a_spi.h"
#include "conversion_constants.h"
#include "meter_config.h"
#include "logger.h"

static const char *TAG = "power_factor";
static bool s_initialized;
static bool s_warned_unverified;
static bool s_warned_invalid_raw;

static bool power_factor_raw_is_valid(uint32_t raw_u32)
{
    return (raw_u32 != 0U) && (raw_u32 != 0xFFFFFFFFU);
}

/**
 * @brief Inicializa modulo de factor de potencia.
 * @param void Sin parametros.
 * @return ESP_OK siempre.
 */
esp_err_t power_factor_init(void)
{
    s_initialized = true;
    s_warned_unverified = false;
    s_warned_invalid_raw = false;
    return ESP_OK;
}

/**
 * @brief Lee APF y lo convierte a PF.
 * @param void Sin parametros.
 * @return Factor de potencia.
 */
float power_factor_get(void)
{
    if (!s_initialized) {
        LOG_WARN(TAG, "power_factor module not initialized");
        return 0.0f;
    }

#if !METER_APF_FORMAT_VERIFIED
    if (!s_warned_unverified) {
        LOG_WARN(TAG, "APF format unverified - returning 0.0f");
        s_warned_unverified = true;
    }
    return 0.0f;
#else
    uint32_t raw_u32 = 0U;
    bool raw_valid = false;
    for (uint8_t attempt = 0U; attempt < 2U; ++attempt) {
        if (ade9153a_read_reg32(ADE9153A_REG_APF, &raw_u32) != ESP_OK) {
            LOG_WARN(TAG, "APF read failed");
            return 0.0f;
        }
        if (power_factor_raw_is_valid(raw_u32)) {
            raw_valid = true;
            break;
        }
    }
    if (!raw_valid) {
        if (!s_warned_invalid_raw) {
            LOG_WARN(TAG, "APF invalid raw (0x00000000/0xFFFFFFFF) - returning 0.0f");
            s_warned_invalid_raw = true;
        }
        return 0.0f;
    }
    const int32_t raw_s32 = (int32_t)raw_u32;  // APF is signed in 5.27 format.

    // PF = crudo / 2^27
    // Fuente: ADE9153AAPI.cpp::ReadPQRegs.
    float pf = METER_POWER_SIGN_CORRECTION * (((float)raw_s32) / 134217728.0f);
    if (pf > 1.0f) {
        pf = 1.0f;
    } else if (pf < -1.0f) {
        pf = -1.0f;
    }
    return pf;
#endif
}
