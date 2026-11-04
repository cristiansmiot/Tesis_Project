#include "data_serializer.h"

#include <stdbool.h>
#include <stdio.h>
#include "esp_system.h"
#include "ade9153a_init.h"
#include "meter_config.h"   // umbrales AC/carga, METER_FW_VERSION
#include "meter_data.h"
#include "mqtt_topics.h"    // mqtt_topics_device_id() para el bn SenML
#include "mqtt_client.h"
#include "node_health.h"    // node_health_get_rssi_dbm(), get_msg_tx(), get_reconnects()
#include "pq_monitor.h"     // PQ_FLAG_SAG, PQ_FLAG_SWELL, PQ_FLAG_FREQ_OOR, PQ_FLAG_OVERTEMP
#include "sim7080g_init.h"  // sim7080g_get_imei()
#include "system_health.h"
#include "logger.h"

static const char *TAG = "data_serializer";
static bool s_initialized;

static const char *data_serializer_reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_POWERON: return "power_on";
        case ESP_RST_EXT: return "external";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "interrupt_wdt";
        case ESP_RST_TASK_WDT: return "task_wdt";
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep_sleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "sdio";
        case ESP_RST_USB: return "usb";
        case ESP_RST_JTAG: return "jtag";
        case ESP_RST_EFUSE: return "efuse";
        case ESP_RST_PWR_GLITCH: return "power_glitch";
        case ESP_RST_CPU_LOCKUP: return "cpu_lockup";
        default: return "unknown";
    }
}

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
// Base Time: UTC seconds when time_valid=true; otherwise zero. Device and
// server timestamps remain separate, including delayed queue delivery.
// Referencia de unidades: https://www.iana.org/assignments/senml/senml.xhtml
// =============================================================================

esp_err_t data_serializer_build_senml_datos(const MeterData_t *snap,
                                             char *out,
                                             size_t out_len)
{
    system_record_base_t base;
    const esp_err_t err = system_record_base_create(SYSTEM_RECORD_MEASUREMENT, &base);
    if (err != ESP_OK) { return err; }
    base.sequence = node_health_data_generated_inc();
    return data_serializer_build_senml_datos_record(snap, &base, out, out_len);
}

esp_err_t data_serializer_build_senml_datos_at(const MeterData_t *snap,
                                               double bt_epoch,
                                               char *out,
                                               size_t out_len)
{
    system_record_base_t base;
    const esp_err_t err = system_record_base_create(SYSTEM_RECORD_MEASUREMENT, &base);
    if (err != ESP_OK) { return err; }
    base.sequence = node_health_data_generated_inc();
    base.time_valid = system_time_epoch_is_valid((int64_t)bt_epoch);
    base.timestamp_utc_s = base.time_valid ? (int64_t)bt_epoch : 0;
    return data_serializer_build_senml_datos_record(snap, &base, out, out_len);
}

esp_err_t data_serializer_build_senml_datos_record(const MeterData_t *snap,
                                                   const system_record_base_t *base,
                                                   char *out, size_t out_len)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((snap == NULL) || (base == NULL) || (out == NULL) || (out_len < 256U)) {
        return ESP_ERR_INVALID_ARG;
    }

    const double bt = base->time_valid ? (double)base->timestamp_utc_s : 0.0;

    // Una temperatura invalida no se publica como una magnitud fisica.
    char temp_record[72] = {0};
    if (snap->temperature_valid != 0U) {
        (void)snprintf(temp_record, sizeof(temp_record),
            "{\"n\":\"temp\",\"u\":\"Cel\",\"v\":%.2f},",
            (double)snap->temperature);
    }

    // Registro base + campos de medicion y bandera de validez termica.
    // Unidades SenML: V, A, W, VAR, VA, / (adim.), Hz, Wh, Cel, count.
    const int len = snprintf(
        out, out_len,
        "[{\"bn\":\"urn:dev:serial:%s:/\",\"bt\":%.3f},"
        "{\"n\":\"boot_id\",\"v\":%lu},"
        "{\"n\":\"seq\",\"v\":%lu},"
        "{\"n\":\"ts_device\",\"v\":%lld},"
        "{\"n\":\"time_valid\",\"vb\":%s},"
        "{\"n\":\"uptime_ms\",\"v\":%lld},"
        "{\"n\":\"fw\",\"vs\":\"" METER_FW_VERSION "\"},"
        "{\"n\":\"cal\",\"v\":%u},"
        "{\"n\":\"vrms\",\"u\":\"V\",\"v\":%.3f},"
        "{\"n\":\"irms\",\"u\":\"A\",\"v\":%.4f},"
        "{\"n\":\"p_act\",\"u\":\"W\",\"v\":%.3f},"
        "{\"n\":\"p_react\",\"u\":\"VAR\",\"v\":%.3f},"
        "{\"n\":\"p_ap\",\"u\":\"VA\",\"v\":%.3f},"
        "{\"n\":\"pf\",\"u\":\"/\",\"v\":%.4f},"
        "{\"n\":\"freq\",\"u\":\"Hz\",\"v\":%.3f},"
        "{\"n\":\"e_act\",\"u\":\"Wh\",\"v\":%.4f},"
        "%s{\"n\":\"temp_ok\",\"vb\":%s},"
        "{\"n\":\"pq\",\"u\":\"count\",\"v\":%lu}]",
        mqtt_topics_device_id(),
        bt,
        (unsigned long)base->boot_id,
        (unsigned long)base->sequence,
        (long long)base->timestamp_utc_s,
        base->time_valid ? "true" : "false",
        (long long)base->uptime_ms,
        (unsigned)snap->calibration_status,
        (double)snap->vrms,
        (double)snap->irms_a,
        (double)snap->active_power_w,
        (double)snap->reactive_power_var,
        (double)snap->apparent_power_va,
        (double)snap->power_factor,
        (double)snap->frequency,
        (double)snap->energy_total_nvs_wh,
        temp_record,
        snap->temperature_valid ? "true" : "false",
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
    const uint32_t ac_losses = node_health_get_ac_losses();
    const uint32_t ac_restores = node_health_get_ac_restores();
    const uint32_t cellular_attempts = node_health_get_cellular_attempts();
    const uint32_t cellular_successes = node_health_get_cellular_successes();
    const uint32_t mqtt_attempts = node_health_get_mqtt_attempts();
    const uint32_t mqtt_successes = node_health_get_mqtt_successes();
    mqtt_diagnostics_t mqtt_diag = {0};
    mqtt_client_get_diagnostics(&mqtt_diag);
    sim7080g_radio_t radio = {0};
    char radio_fields[256] = {0};
    if (cellular_ok && (node_health_refresh_radio(&radio) == ESP_OK)) {
        (void)snprintf(radio_fields, sizeof(radio_fields),
            "{\"n\":\"rsrp\",\"v\":%.2f},"
            "{\"n\":\"rsrq\",\"v\":%.2f},"
            "{\"n\":\"snrr\",\"v\":%.2f},"
            "{\"n\":\"sinr\",\"v\":%.2f},",
            (double)radio.rsrp_dbm, (double)radio.rsrq_db,
            (double)radio.rssnr_raw, (double)radio.sinr_db);
        rssi_dbm = (int8_t)radio.rssi_dbm;
    }
    system_record_base_t status_base = {0};
    (void)system_record_base_create(SYSTEM_RECORD_MEASUREMENT, &status_base);
    // bt=0: backend asigna timestamp UTC al recibir (ver /datos).
    const double bt = 0.0;
    const esp_reset_reason_t reset_reason = esp_reset_reason();

    // Sin "online": siempre valia true y el backend ya lo asume por defecto
    // (data.get("online", True)). /estado rondaba 1014 de los 1024 bytes que
    // acepta SMPUB y los contadores crecen con el uptime; estos 25 bytes
    // evitan que el mensaje se rechace por tamano en corridas largas.
    const int len = snprintf(
        out, out_len,
        "[{\"bn\":\"urn:dev:serial:%s:/\",\"bt\":%.3f},"
        "{\"n\":\"ac\",\"vb\":%s},"
        "{\"n\":\"carga\",\"vb\":%s},"
        "{\"n\":\"red_cel\",\"vb\":%s},"
        "{\"n\":\"mqtt\",\"vb\":%s},"
        "{\"n\":\"ade\",\"vb\":%s},"
        "{\"n\":\"ade_perd\",\"v\":%lu},"
        "{\"n\":\"ade_rec_ok\",\"v\":%lu},"
        "{\"n\":\"acl\",\"v\":%lu},"
        "{\"n\":\"acr\",\"v\":%lu},"
        "{\"n\":\"red_int\",\"v\":%lu},"
        "{\"n\":\"red_ok\",\"v\":%lu},"
        "{\"n\":\"mqtt_int\",\"v\":%lu},"
        "{\"n\":\"mqtt_ok_cnt\",\"v\":%lu},"
        "{\"n\":\"dg\",\"v\":%lu},"
        "{\"n\":\"dpa\",\"v\":%lu},"
        "{\"n\":\"dpo\",\"v\":%lu},"
        "{\"n\":\"rec\",\"v\":%lu},"
        "{\"n\":\"creg\",\"v\":%d},"
        "{\"n\":\"pdp\",\"v\":%d},"
        "{\"n\":\"ms\",\"v\":%d},"
        "{\"n\":\"pe\",\"v\":%ld},"
        "{\"n\":\"re\",\"v\":%ld},"
        "{\"n\":\"rm\",\"v\":%lu},"
        "{\"n\":\"rs\",\"vs\":\"%s\"},"
        "{\"n\":\"boot_id\",\"v\":%lu},"
        "{\"n\":\"uptime_ms\",\"v\":%lld},"
        "{\"n\":\"ts_device\",\"v\":%lld},"
        "{\"n\":\"time_valid\",\"vb\":%s},"
        "{\"n\":\"rr\",\"v\":%d},"
        "{\"n\":\"rrn\",\"vs\":\"%s\"},"
        "{\"n\":\"rv\",\"vb\":%s},%s"
        "{\"n\":\"cal_ok\",\"vb\":%s},"
        "{\"n\":\"rssi_dbm\",\"u\":\"dBm\",\"v\":%d},"
        "{\"n\":\"imei\",\"vs\":\"%s\"},"
        "{\"n\":\"fw\",\"vs\":\"" METER_FW_VERSION "\"}]",
        mqtt_topics_device_id(),
        bt,
        ac_ok ? "true" : "false",
        carga ? "true" : "false",
        cellular_ok ? "true" : "false",
        mqtt_ok ? "true" : "false",
        ade_ok ? "true" : "false",
        (unsigned long)ade_losses,
        (unsigned long)ade_recovery_successes,
        (unsigned long)ac_losses,
        (unsigned long)ac_restores,
        (unsigned long)cellular_attempts,
        (unsigned long)cellular_successes,
        (unsigned long)mqtt_attempts,
        (unsigned long)mqtt_successes,
        (unsigned long)node_health_get_data_generated(),
        (unsigned long)node_health_get_data_publish_attempts(),
        (unsigned long)node_health_get_msg_tx(),
        (unsigned long)node_health_get_reconnects(),
        mqtt_diag.cereg_stat,
        mqtt_diag.cnact_state,
        mqtt_diag.smstate,
        (long)mqtt_diag.last_publish_error,
        (long)mqtt_diag.last_recovery_error,
        (unsigned long)mqtt_diag.last_recovery_ms,
        mqtt_diag.recovery_stage,
        (unsigned long)status_base.boot_id,
        (long long)status_base.uptime_ms,
        (long long)status_base.timestamp_utc_s,
        status_base.time_valid ? "true" : "false",
        (int)reset_reason,
        data_serializer_reset_reason_name(reset_reason),
        radio.valid ? "true" : "false", radio_fields,
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
        "[{\"bn\":\"urn:dev:serial:%s:/\",\"bt\":%.3f},"
        "{\"n\":\"tipo\",\"vs\":\"%s\"},"
        "{\"n\":\"vrms\",\"u\":\"V\",\"v\":%.3f},"
        "{\"n\":\"pq\",\"u\":\"count\",\"v\":%lu}]",
        mqtt_topics_device_id(),
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

esp_err_t data_serializer_build_senml_system_event(const system_event_t *event,
                                                   char *out,
                                                   size_t out_len)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((event == NULL) || (out == NULL) || (out_len < 384U)) {
        return ESP_ERR_INVALID_ARG;
    }

    char event_id[24];
    (void)snprintf(event_id, sizeof(event_id), "%08lx-%08lx",
                   (unsigned long)event->boot_id,
                   (unsigned long)event->sequence);
    const double bt = event->time_valid ? (double)event->timestamp_utc_s : 0.0;

    const int len = snprintf(
        out, out_len,
        "[{\"bn\":\"urn:dev:serial:%s:/\",\"bt\":%.3f},"
        "{\"n\":\"event_id\",\"vs\":\"%s\"},"
        "{\"n\":\"boot_id\",\"v\":%lu},"
        "{\"n\":\"seq\",\"v\":%lu},"
        "{\"n\":\"ts_device\",\"v\":%lld},"
        "{\"n\":\"fw\",\"vs\":\"" METER_FW_VERSION "\"},"
        "{\"n\":\"module\",\"vs\":\"%s\"},"
        "{\"n\":\"code\",\"vs\":\"%s\"},"
        "{\"n\":\"severity\",\"vs\":\"%s\"},"
        "{\"n\":\"transition\",\"vs\":\"%s\"},"
        "{\"n\":\"state\",\"vs\":\"%s\"},"
        "{\"n\":\"time_valid\",\"vb\":%s},"
        "{\"n\":\"uptime_ms\",\"u\":\"ms\",\"v\":%lld},"
        "{\"n\":\"error\",\"v\":%ld},"
        "{\"n\":\"count\",\"u\":\"count\",\"v\":%lu}]",
        mqtt_topics_device_id(),
        bt,
        event_id,
        (unsigned long)event->boot_id,
        (unsigned long)event->sequence,
        (long long)event->timestamp_utc_s,
        system_module_name(event->module),
        system_event_code_name(event->code),
        system_severity_name(event->severity),
        system_event_transition_name(event->transition),
        system_state_name(event->resulting_state),
        event->time_valid ? "true" : "false",
        (long long)(event->uptime_us / 1000LL),
        (long)event->error,
        (unsigned long)event->occurrence_count);
    if ((len <= 0) || ((size_t)len >= out_len)) {
        LOG_WARN(TAG, "SenML system event truncado (len=%d out_len=%u)",
                 len, (unsigned)out_len);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}


