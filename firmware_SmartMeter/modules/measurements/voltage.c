#include "voltage.h"

#include <stdbool.h>
#include <stdint.h>
#include "ade9153a_regs.h"
#include "ade9153a_spi.h"
#include "conversion_constants.h"
#include "meter_config.h"
#include "logger.h"

static const char *TAG = "voltage";
static float s_target_avcc;
static bool s_initialized;
static bool s_warned_vpeak;
static bool s_warned_vrms_unverified;
static bool s_warned_aperiod_invalid;
static bool s_warned_vrms_invalid_raw;

static float voltage_avrms_raw_to_vrms(uint32_t raw_u32)
{
    return ((float)raw_u32) * s_target_avcc * 1e-9f;
}

static bool voltage_avrms_raw_is_valid(uint32_t raw_u32)
{
    if ((raw_u32 == 0U) || (raw_u32 == 0xFFFFFFFFU)) {
        return false;
    }
    if (!s_initialized || (s_target_avcc <= 0.0f)) {
        return true;
    }

    const float vrms = voltage_avrms_raw_to_vrms(raw_u32);
    return (vrms >= 0.0f) && (vrms <= METER_VRMS_SANITY_MAX_V);
}

static esp_err_t voltage_read_avrms_raw_filtered(uint32_t *raw_out)
{
    if (raw_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Retry corto para amortiguar lecturas espurias intermitentes del bus.
    for (uint8_t attempt = 0U; attempt < 2U; ++attempt) {
        uint32_t raw_u32 = 0U;
        const esp_err_t err = ade9153a_read_reg32(ADE9153A_REG_AVRMS, &raw_u32);
        if (err != ESP_OK) {
            return err;
        }
        if (voltage_avrms_raw_is_valid(raw_u32)) {
            *raw_out = raw_u32;
            return ESP_OK;
        }
    }

    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t voltage_read_aperiod_raw(uint32_t *raw_out)
{
    if (raw_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return ade9153a_read_reg32(ADE9153A_REG_APERIOD, raw_out);
}

/**
 * @brief Inicializa constantes del modulo de voltaje.
 * @param target_avcc Constante de conversion [nV/code].
 * @return ESP_OK en caso de exito.
 */
esp_err_t voltage_init(float target_avcc)
{
    if (target_avcc <= 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    s_target_avcc = target_avcc;
    s_initialized = true;
    s_warned_vpeak = false;
    s_warned_vrms_unverified = false;
    s_warned_aperiod_invalid = false;
    s_warned_vrms_invalid_raw = false;
    return ESP_OK;
}

/**
 * @brief Lee AVRMS y lo convierte a volts.
 * @param void Sin parametros.
 * @return Voltaje RMS en volts.
 */
float voltage_get_vrms(void)
{
    if (!s_initialized) {
        LOG_WARN(TAG, "voltage module not initialized");
        return 0.0f;
    }

#if !METER_VHEADROOM_VERIFIED && !METER_USE_ADI_API_CC_DEFAULTS
    if (!s_warned_vrms_unverified) {
        LOG_WARN(TAG, "VHEADROOM unverified and ADI defaults disabled - VRMS disabled (returning 0.0f)");
        s_warned_vrms_unverified = true;
    }
    return 0.0f;
#else
    uint32_t raw_u32 = 0U;
    const esp_err_t err = voltage_read_avrms_raw_filtered(&raw_u32);
    if (err == ESP_ERR_INVALID_RESPONSE) {
        if (!s_warned_vrms_invalid_raw) {
            LOG_WARN(TAG, "AVRMS invalid raw/plausibility - VRMS forced to 0.0V");
            s_warned_vrms_invalid_raw = true;
        }
        return 0.0f;
    }
    if (err != ESP_OK) {
        LOG_WARN(TAG, "AVRMS read failed");
        return 0.0f;
    }

    // AVRMS [V] = crudo * TARGET_AVCC[nV/code] * 1e-9
    return voltage_avrms_raw_to_vrms(raw_u32);
#endif
}

/**
 * @brief Obtiene voltaje pico.
 * @param void Sin parametros.
 * @return Voltaje pico en volts.
 */
float voltage_get_vpeak(void)
{
    // TODO: verificar escala de VPEAK contra ADE9153A Datasheet Rev.0.
    if (!s_warned_vpeak) {
        LOG_WARN(TAG, "VPEAK conversion unverified - returning 0.0f");
        s_warned_vpeak = true;
    }
    return 0.0f;
}

/**
 * @brief Lee APERIOD y calcula frecuencia de linea.
 * @param void Sin parametros.
 * @return Frecuencia en Hz.
 */
float voltage_get_frequency(void)
{
    if (!s_initialized) {
        LOG_WARN(TAG, "voltage module not initialized");
        return 0.0f;
    }

    uint32_t raw_u32 = 0U;
    if (voltage_read_aperiod_raw(&raw_u32) != ESP_OK) {
        LOG_WARN(TAG, "APERIOD read failed");
        return 0.0f;
    }

    if ((raw_u32 == 0U) || (raw_u32 >= 0x00FFFFFFU)) {
        if (!s_warned_aperiod_invalid) {
            LOG_WARN(TAG, "APERIOD invalid raw=0x%08lx - frequency forced to 0.0Hz", (unsigned long)raw_u32);
            s_warned_aperiod_invalid = true;
        }
        return 0.0f;
    }

    // Frequency[Hz] = (4000 * 65536) / (APERIOD + 1)
    // Fuente: ADE9153AAPI.cpp::ReadPQRegs
    return (4000.0f * 65536.0f) / ((float)raw_u32 + 1.0f);
}

/**
 * @brief Lee AVRMS crudo para diagnostico de presencia de linea.
 * @param raw_out Salida del valor crudo del registro AVRMS.
 * @return ESP_OK en caso de exito.
 */
esp_err_t voltage_get_vrms_raw(uint32_t *raw_out)
{
    if (raw_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return voltage_read_avrms_raw_filtered(raw_out);
}

esp_err_t voltage_get_aperiod_raw(uint32_t *raw_out)
{
    if (raw_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return voltage_read_aperiod_raw(raw_out);
}




