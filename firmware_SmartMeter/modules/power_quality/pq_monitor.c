#include "pq_monitor.h"

#include <stdbool.h>
#include "meter_config.h"
#include "sag_swell.h"
#include "logger.h"

static bool s_initialized;
static uint32_t s_flags;

/**
 * @brief Inicializa monitor de calidad de potencia.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t pq_monitor_init(void)
{
    s_flags = 0U;
    s_initialized = true;
    return ESP_OK;
}

uint32_t pq_monitor_get_flags(void)
{
    return s_flags;
}

uint32_t pq_monitor_process_snapshot(const MeterData_t *snap)
{
    if (!s_initialized || (snap == NULL)) {
        return s_flags;
    }

    uint32_t flags = 0U;
    const bool freq_valid =
        (snap->frequency >= METER_AC_PRESENT_FREQ_MIN_HZ) &&
        (snap->frequency <= METER_AC_PRESENT_FREQ_MAX_HZ);
    const bool ac_present = (snap->vrms >= METER_AC_PRESENT_VRMS_MIN_V) && freq_valid;

    const uint32_t ss_flags = sag_swell_eval(snap->vrms, ac_present);
    if ((ss_flags & SAG_SWELL_FLAG_SAG) != 0U) {
        flags |= PQ_FLAG_SAG;
    }
    if ((ss_flags & SAG_SWELL_FLAG_SWELL) != 0U) {
        flags |= PQ_FLAG_SWELL;
    }

    if ((snap->vrms >= METER_AC_PRESENT_VRMS_MIN_V) && !freq_valid) {
        flags |= PQ_FLAG_FREQ_OOR;
    }
    if (snap->temperature >= METER_OVERTEMP_C) {
        flags |= PQ_FLAG_OVERTEMP;
    }

    s_flags = flags;
    return s_flags;
}

