#include "sag_swell.h"

#include <stdbool.h>
#include "meter_config.h"
#include "logger.h"

static const char *TAG = "sag_swell";
static bool s_initialized;
static bool s_sag_active;
static bool s_swell_active;
static float s_sag_enter_v;
static float s_sag_exit_v;
static float s_swell_enter_v;
static float s_swell_exit_v;

/**
 * @brief Inicializa detector de sag/swell.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t sag_swell_init(void)
{
    if ((METER_SAG_THRESHOLD_PCT <= 0.0f) || (METER_SWELL_THRESHOLD_PCT <= 0.0f)) {
        LOG_WARN(TAG, "invalid sag/swell thresholds");
        return ESP_ERR_INVALID_ARG;
    }

    s_sag_enter_v = METER_VNOM_V * (1.0f - METER_SAG_THRESHOLD_PCT);
    s_sag_exit_v = METER_VNOM_V * (1.0f - (METER_SAG_THRESHOLD_PCT * 0.50f));
    s_swell_enter_v = METER_VNOM_V * (1.0f + METER_SWELL_THRESHOLD_PCT);
    s_swell_exit_v = METER_VNOM_V * (1.0f + (METER_SWELL_THRESHOLD_PCT * 0.50f));
    s_sag_active = false;
    s_swell_active = false;
    s_initialized = true;
    return ESP_OK;
}

uint32_t sag_swell_eval(float vrms, bool ac_present)
{
    if (!s_initialized || !ac_present || (vrms <= 0.0f)) {
        s_sag_active = false;
        s_swell_active = false;
        return 0U;
    }

    if (!s_sag_active && (vrms <= s_sag_enter_v)) {
        s_sag_active = true;
    } else if (s_sag_active && (vrms >= s_sag_exit_v)) {
        s_sag_active = false;
    }

    if (!s_swell_active && (vrms >= s_swell_enter_v)) {
        s_swell_active = true;
    } else if (s_swell_active && (vrms <= s_swell_exit_v)) {
        s_swell_active = false;
    }

    uint32_t flags = 0U;
    if (s_sag_active) {
        flags |= SAG_SWELL_FLAG_SAG;
    }
    if (s_swell_active) {
        flags |= SAG_SWELL_FLAG_SWELL;
    }
    return flags;
}

