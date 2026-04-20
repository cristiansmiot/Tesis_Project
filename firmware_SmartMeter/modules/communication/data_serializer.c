#include "data_serializer.h"

#include <stdbool.h>
#include <stdio.h>
#include "ade9153a_init.h"
#include "meter_config.h"   // METER_MQTT_CLIENT_ID, umbrales AC/carga, METER_FW_VERSION
#include "meter_data.h"
#include "node_health.h"    // node_health_get_rssi_dbm(), get_msg_tx(), get_reconnects()
#include "pq_monitor.h"     // PQ_FLAG_SAG, PQ_FLAG_SWELL, PQ_FLAG_FREQ_OOR, PQ_FLAG_OVERTEMP
#include "sim7080g_init.h"  // sim7080g_get_imei()
#include "logger.h"

static const char *TAG = "data_serializer";
static bool s_initialized;

static const char *data_serializer_ade_action_name(ade9153a_recovery_action_t action)
{
    switch (action) {
        case ADE9153A_RECOVERY_ACTION_PROBE:
            return "probe";
        case ADE9153A_RECOVERY_ACTION_SOFT_INIT:
            return "soft_init";
        case ADE9153A_RECOVERY_ACTION_SPI_REINIT:
            return "spi_reinit";
        default:
            return "unknown";
    }
}

/**
 * @brief Inicializa serializer de telemetria.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t data_serializer_init(void)
{
    s_initialized = true;
    return ESP_OK;
}

esp_err_t data_serializer_build_snapshot_json(const MeterData_t *snap,
                                              char *out_json,
                                              size_t out_len)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((snap == NULL) || (out_json == NULL) || (out_len < 160U)) {
        return ESP_ERR_INVALID_ARG;
    }

    ade9153a_diag_t ade = {0};
    (void)ade9153a_get_diag(&ade);

    // Contrato JSON publicado al broker:
    // - q_var: potencia reactiva.
    // - s_va: potencia aparente.
    // - e_wh: alias legacy del total persistido (no del acumulador E_hw).
    // - temp_c/temp_ok: temperatura del ADE y validez de la conversion.
    // - ade_*: estado de salud y recuperacion del ADE para diagnostico remoto.
    // - cal: bitmask CALIB_STATUS_* proveniente de task_calibration.
    const int len = snprintf(
        out_json,
        out_len,
        "{\"ts_us\":%lld,\"vrms\":%.3f,\"irms_a\":%.3f,\"p_w\":%.3f,"
        "\"q_var\":%.3f,\"s_va\":%.3f,\"pf\":%.4f,\"f_hz\":%.3f,"
        "\"e_wh\":%.6f,\"e_soft_wh\":%.6f,\"e_nvs_wh\":%.6f,"
        "\"temp_c\":%.3f,\"temp_ok\":%u,\"pq\":%lu,\"cal\":%u,"
        "\"ade_ok\":%u,\"ade_bus\":%u,\"ade_probe_err\":%ld,"
        "\"ade_rec\":%lu,\"ade_act\":\"%s\"}",
        (long long)snap->timestamp_us,
        (double)snap->vrms,
        (double)snap->irms_a,
        (double)snap->active_power_w,
        (double)snap->reactive_power_var,
        (double)snap->apparent_power_va,
        (double)snap->power_factor,
        (double)snap->frequency,
        (double)snap->energy_wh,
        (double)snap->energy_soft_wh,
        (double)snap->energy_total_nvs_wh,
        (double)snap->temperature,
        (unsigned)snap->temperature_valid,
        (unsigned long)snap->pq_status,
        (unsigned)snap->calibration_status,
        (unsigned)(ade.ready ? 1U : 0U),
        (unsigned)(ade.bus_disconnected ? 1U : 0U),
        (long)ade.probe_err,
        (unsigned long)ade.recovery_count,
        data_serializer_ade_action_name(ade.last_action));

    if ((len <= 0) || ((size_t)len >= out_len)) {
        LOG_WARN(TAG, "JSON build truncated");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t data_serializer_build_current_json(char *out_json, size_t out_len)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const MeterData_t snap = meter_data_get_snapshot();
    return data_serializer_build_snapshot_json(&snap, out_json, out_len);
}

// =============================================================================
// SenML RFC 8428 ? Formato activo de telemetria
// Base Name: "urn:dev:mac:<METER_MQTT_CLIENT_ID>:/"
// Base Time: segundos desde arranque del dispositivo (monotono, no UTC).
//            El backend asigna timestamp UTC al momento de recepcion.
// Referencia de unidades: https://www.iana.org/assignments/senml/senml.xhtml
// =============================================================================

esp_err_t data_serializer_build_senml_datos(const MeterData_t *snap,
                                             char *out,
                                             size_t out_len)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((snap == NULL) || (out == NULL) || (out_len < 256U)) {
        return ESP_ERR_INVALID_ARG;
    }

    // bt=0: RFC 8428 deja que el receptor use el tiempo de recepcion como
    // referencia. El nodo no dispone de RTC sincronizado por NTP/celular, por
    // lo que publicar uptime como bt induciria al backend a interpretarlo
    // como "hace X segundos" (RFC 8428 sec 4.5.3: valores < 2^28 son tiempos
    // relativos). El backend asigna timestamp UTC al recibir.
    const double bt = 0.0;

    // Registro base + 10 campos de medicion electrica.
    // Unidades SenML: V, A, W, VAR, VA, / (adim.), Hz, Wh, Cel, count.
    const int len = snprintf(
        out, out_len,
        "[{\"bn\":\"urn:dev:serial:" METER_MQTT_CLIENT_ID ":/\",\"bt\":%.3f},"
        "{\"n\":\"vrms\",\"u\":\"V\",\"v\":%.3f},"
        "{\"n\":\"irms\",\"u\":\"A\",\"v\":%.4f},"
        "{\"n\":\"p_act\",\"u\":\"W\",\"v\":%.3f},"
        "{\"n\":\"p_react\",\"u\":\"VAR\",\"v\":%.3f},"
        "{\"n\":\"p_ap\",\"u\":\"VA\",\"v\":%.3f},"
        "{\"n\":\"pf\",\"u\":\"/\",\"v\":%.4f},"
        "{\"n\":\"freq\",\"u\":\"Hz\",\"v\":%.3f},"
        "{\"n\":\"e_act\",\"u\":\"Wh\",\"v\":%.4f},"
        "{\"n\":\"temp\",\"u\":\"Cel\",\"v\":%.2f},"
        "{\"n\":\"pq\",\"u\":\"count\",\"v\":%lu}]",
        bt,
        (double)snap->vrms,
        (double)snap->irms_a,
        (double)snap->active_power_w,
        (double)snap->reactive_power_var,
        (double)snap->apparent_power_va,
        (double)snap->power_factor,
        (double)snap->frequency,
        (double)snap->energy_total_nvs_wh,
        (double)snap->temperature,
        (unsigned long)snap->pq_status);

    if ((len <= 0) || ((size_t)len >= out_len)) {
        LOG_WARN(TAG, "SenML /datos truncado (len=%d out_len=%u)", len, (unsigned)out_len);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t data_serializer_build_senml_estado(const MeterData_t *snap,
                                              bool cellular_ok,
                                              bool mqtt_ok,
                                              bool cal_ok,
                                              char *out,
                                              size_t out_len)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((snap == NULL) || (out == NULL) || (out_len < 256U)) {
        return ESP_ERR_INVALID_ARG;
    }

    ade9153a_diag_t ade = {0};
    (void)ade9153a_get_diag(&ade);

    const bool ade_ok = ade.ready && !ade.bus_disconnected;
    int8_t rssi_dbm = -127;
    if (cellular_ok) {
        (void)node_health_get_rssi_dbm(&rssi_dbm);
    }

    // Estado electrico derivado del snapshot.
    const bool ac_ok = (snap->ac_present != 0U);
    const bool carga = ac_ok && ((double)snap->irms_a > (double)METER_NO_LOAD_IRMS_A_MAX_A);

    const uint32_t ade_losses = node_health_get_ade_losses();
    const uint32_t ade_recovery_successes = node_health_get_ade_recovery_successes();
    const uint32_t cellular_attempts = node_health_get_cellular_attempts();
    const uint32_t cellular_successes = node_health_get_cellular_successes();
    const uint32_t mqtt_attempts = node_health_get_mqtt_attempts();
    const uint32_t mqtt_successes = node_health_get_mqtt_successes();
    // bt=0: backend asigna timestamp UTC al recibir (ver /datos).
    const double bt = 0.0;

    const int len = snprintf(
        out, out_len,
        "[{\"bn\":\"urn:dev:serial:" METER_MQTT_CLIENT_ID ":/\",\"bt\":%.3f},"
        "{\"n\":\"online\",\"vb\":true},"
        "{\"n\":\"ac\",\"vb\":%s},"
        "{\"n\":\"carga\",\"vb\":%s},"
        "{\"n\":\"red_cel\",\"vb\":%s},"
        "{\"n\":\"mqtt\",\"vb\":%s},"
        "{\"n\":\"ade\",\"vb\":%s},"
        "{\"n\":\"ade_perd\",\"u\":\"count\",\"v\":%lu},"
        "{\"n\":\"ade_rec_ok\",\"u\":\"count\",\"v\":%lu},"
        "{\"n\":\"red_int\",\"u\":\"count\",\"v\":%lu},"
        "{\"n\":\"red_ok\",\"u\":\"count\",\"v\":%lu},"
        "{\"n\":\"mqtt_int\",\"u\":\"count\",\"v\":%lu},"
        "{\"n\":\"mqtt_ok_cnt\",\"u\":\"count\",\"v\":%lu},"
        "{\"n\":\"cal_ok\",\"vb\":%s},"
        "{\"n\":\"rssi_dbm\",\"u\":\"dBm\",\"v\":%d},"
        "{\"n\":\"imei\",\"vs\":\"%s\"},"
        "{\"n\":\"fw\",\"vs\":\"" METER_FW_VERSION "\"}]",
        bt,
        ac_ok ? "true" : "false",
        carga ? "true" : "false",
        cellular_ok ? "true" : "false",
        mqtt_ok ? "true" : "false",
        ade_ok ? "true" : "false",
        (unsigned long)ade_losses,
        (unsigned long)ade_recovery_successes,
        (unsigned long)cellular_attempts,
        (unsigned long)cellular_successes,
        (unsigned long)mqtt_attempts,
        (unsigned long)mqtt_successes,
        cal_ok ? "true" : "false",
        (int)rssi_dbm,
        sim7080g_get_imei());

    if ((len <= 0) || ((size_t)len >= out_len)) {
        LOG_WARN(TAG, "SenML /estado truncado (len=%d out_len=%u)", len, (unsigned)out_len);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t data_serializer_build_senml_alerta(const MeterData_t *snap,
                                              uint32_t pq_flags,
                                              char *out,
                                              size_t out_len)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((snap == NULL) || (out == NULL) || (out_len < 128U)) {
        return ESP_ERR_INVALID_ARG;
    }

    // Prioridad de reporte: sag > swell > freq_oor > overtemp.
    const char *tipo;
    if ((pq_flags & PQ_FLAG_SAG) != 0U) {
        tipo = "sag";
    } else if ((pq_flags & PQ_FLAG_SWELL) != 0U) {
        tipo = "swell";
    } else if ((pq_flags & PQ_FLAG_FREQ_OOR) != 0U) {
        tipo = "freq_oor";
    } else if ((pq_flags & PQ_FLAG_OVERTEMP) != 0U) {
        tipo = "overtemp";
    } else {
        tipo = "pq_event";
    }

    // bt=0: backend asigna timestamp UTC al recibir (ver /datos).
    const double bt = 0.0;

    const int len = snprintf(
        out, out_len,
        "[{\"bn\":\"urn:dev:serial:" METER_MQTT_CLIENT_ID ":/\",\"bt\":%.3f},"
        "{\"n\":\"tipo\",\"vs\":\"%s\"},"
        "{\"n\":\"vrms\",\"u\":\"V\",\"v\":%.3f},"
        "{\"n\":\"pq\",\"u\":\"count\",\"v\":%lu}]",
        bt,
        tipo,
        (double)snap->vrms,
        (unsigned long)pq_flags);

    if ((len <= 0) || ((size_t)len >= out_len)) {
        LOG_WARN(TAG, "SenML /alerta truncado (len=%d out_len=%u)", len, (unsigned)out_len);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}


