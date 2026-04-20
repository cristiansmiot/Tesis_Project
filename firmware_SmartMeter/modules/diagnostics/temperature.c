#include "temperature.h"

#include <stdbool.h>
#include <stdint.h>
#include "ade9153a_hal.h"
#include "ade9153a_regs.h"
#include "ade9153a_spi.h"
#include "meter_config.h"
#include "logger.h"

static const char *TAG = "temperature";
static bool s_initialized;
static bool s_warned_read_fail;
static bool s_warned_invalid_trim;
static bool s_warned_invalid_temp;

static const uint16_t k_temp_cfg_start = 0x000CU;

/**
 * @brief Inicializa modulo de temperatura.
 * @param void Sin parametros.
 * @return ESP_OK siempre.
 */
esp_err_t temperature_init(void)
{
    s_initialized = true;
    s_warned_read_fail = false;
    s_warned_invalid_trim = false;
    s_warned_invalid_temp = false;
    return ESP_OK;
}

/**
 * @brief Lee temperatura interna y retorna valor fisico.
 * @param out_temp_c Salida en grados Celsius.
 * @return ESP_OK si la lectura es valida.
 */
esp_err_t temperature_read_internal(float *out_temp_c)
{
    if (out_temp_c == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        LOG_WARN(TAG, "temperature module not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    const ADE9153A_HAL_t *hal = ade9153a_hal_get();
    if (hal == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ade9153a_write_reg16(ADE9153A_REG_TEMP_CFG, k_temp_cfg_start);
    if (err != ESP_OK) {
        if (!s_warned_read_fail) {
            LOG_WARN(TAG, "TEMP_CFG write failed");
            s_warned_read_fail = true;
        }
        return err;
    }

    // Referencia ADI ADE9153AAPI.cpp::ReadTemperature: esperar 10 ms tras iniciar el ciclo.
    hal->platform_delay_ms(10U);

    uint32_t trim = 0U;
    err = ade9153a_read_reg32(ADE9153A_REG_TEMP_TRIM, &trim);
    if (err != ESP_OK) {
        if (!s_warned_read_fail) {
            LOG_WARN(TAG, "TEMP_TRIM read failed");
            s_warned_read_fail = true;
        }
        return err;
    }

    uint16_t raw_u16 = 0U;
    err = ade9153a_read_reg16(ADE9153A_REG_TEMP_RSLT, &raw_u16);
    if (err != ESP_OK) {
        if (!s_warned_read_fail) {
            LOG_WARN(TAG, "TEMP_RSLT read failed");
            s_warned_read_fail = true;
        }
        return err;
    }

    const uint16_t gain = (uint16_t)(trim & 0xFFFFU);
    const uint16_t offset = (uint16_t)((trim >> 16U) & 0xFFFFU);
    if ((gain == 0U) || (gain == 0xFFFFU)) {
        if (!s_warned_invalid_trim) {
            LOG_WARN(TAG, "TEMP_TRIM invalid gain=0x%04X offset=0x%04X", (unsigned)gain, (unsigned)offset);
            s_warned_invalid_trim = true;
        }
        return ESP_ERR_INVALID_RESPONSE;
    }

    const int16_t temp_reg = (int16_t)raw_u16;
    *out_temp_c = ((float)offset / 32.0f) - (((float)temp_reg * (float)gain) / 131072.0f);
    if ((*out_temp_c < METER_TEMP_SANITY_MIN_C) || (*out_temp_c > METER_TEMP_SANITY_MAX_C)) {
        if (!s_warned_invalid_temp) {
            LOG_WARN(TAG, "temperature out of sanity range: %.2fC", (double)*out_temp_c);
            s_warned_invalid_temp = true;
        }
        return ESP_ERR_INVALID_RESPONSE;
    }

    s_warned_read_fail = false;
    s_warned_invalid_trim = false;
    s_warned_invalid_temp = false;
    return ESP_OK;
}

/**
 * @brief Wrapper legacy para obtener la temperatura ADE.
 * @param void Sin parametros.
 * @return Temperatura en Celsius o 0.0f si falla la lectura.
 */
float temperature_get_internal(void)
{
    float temp_c = 0.0f;
    if (temperature_read_internal(&temp_c) != ESP_OK) {
        return 0.0f;
    }
    return temp_c;
}





