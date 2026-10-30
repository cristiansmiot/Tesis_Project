#include "mqtt_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "meter_config.h"
#if (METER_MQTT_USE_TLS != 0)
#include "mqtt_ca_cert.h"
#endif
#include "mqtt_topics.h"
#include "node_health.h"
#include "sim7080g_hal.h"
#include "sim7080g_init.h"
#include "logger.h"

static const char *TAG = "mqtt_client";
static bool s_configured;
static bool s_connected;
static bool s_force_reconfigure;
// Duracion del ultimo SMCONN exitoso, para comparar el overhead del
// handshake TLS vs. conexion en claro (capitulo de ciberseguridad).
static uint32_t s_last_connect_ms;
static int64_t s_last_op_not_allowed_pdp_cycle_us;
// Instante del ultimo SMCONN que vencio sin confirmar sesion; el modem puede
// completarlo minutos despues y esa sesion se adopta (ver configure_profile).
static int64_t s_smconn_pending_us;
static const uint32_t k_mqtt_state_timeout_ms = 1000U;
static const uint32_t k_mqtt_diag_timeout_ms = 1000U;
#define MQTT_OP_NOT_ALLOWED_PDP_CYCLE_COOLDOWN_MS METER_MQTT_PDP_CYCLE_COOLDOWN_MS

#define AT_SMCONF_URL_FMT       "AT+SMCONF=\"URL\",\"%s\",%u"
#define AT_SMCONF_CLIENT_FMT    "AT+SMCONF=\"CLIENTID\",\"%s\""
#define AT_SMCONF_USER_FMT      "AT+SMCONF=\"USERNAME\",\"%s\""
#define AT_SMCONF_PASS_FMT      "AT+SMCONF=\"PASSWORD\",\"%s\""
#define AT_SMCONF_KEEP_FMT      "AT+SMCONF=\"KEEPTIME\",%u"
#define AT_SMCONF_CLEANSS       "AT+SMCONF=\"CLEANSS\",1"
#define AT_SMCONF_QOS0          "AT+SMCONF=\"QOS\",0"
#define AT_SMCONN               "AT+SMCONN"
#define AT_SMDISC               "AT+SMDISC"
// TLS: archivo CA en el FS del modem y vinculo con la sesion MQTT.
// CFSWFILE escribe en el indice 3 (/customer/), unico directorio que
// AT+CSSLCFG="CONVERT" acepta como origen de certificados.
#define AT_CFSINIT              "AT+CFSINIT"
#define AT_CFSTERM              "AT+CFSTERM"
#define AT_CFSWFILE_FMT         "AT+CFSWFILE=3,\"%s\",0,%u,%u"
#define AT_CSSLCFG_VER_FMT      "AT+CSSLCFG=\"SSLVERSION\",%u,%u"
#define AT_CSSLCFG_CONVERT_FMT  "AT+CSSLCFG=\"CONVERT\",2,\"%s\""
#define AT_CSSLCFG_SNI_FMT      "AT+CSSLCFG=\"SNI\",%u,\"%s\""
#define AT_SMSSL_ON_FMT         "AT+SMSSL=%u,\"%s\",\"\""
#define AT_SMSSL_OFF            "AT+SMSSL=0"
#define AT_SMSTATE_QUERY        "AT+SMSTATE?"
#define AT_SMPUB_FMT            "AT+SMPUB=\"%s\",%d,%d,%d"
#define AT_CEREG_QUERY          "AT+CEREG?"
#define AT_CGATT_QUERY          "AT+CGATT?"
#define AT_CNACT_QUERY          "AT+CNACT?"
#define AT_CSQ_QUERY            "AT+CSQ"
#define AT_CPSI_QUERY           "AT+CPSI?"
#define MQTT_SMCONN_TIMEOUT_MS  METER_MQTT_SMCONN_TIMEOUT_MS
#define MQTT_SMCONN_RETRIES     METER_MQTT_SMCONN_RETRIES

static mqtt_diagnostics_t s_diag = {
    .cereg_stat = -1,
    .cnact_state = -1,
    .smstate = -1,
    .last_publish_error = ESP_OK,
    .last_recovery_error = ESP_OK,
    .recovery_stage = "idle",
};

static void mqtt_client_set_recovery_stage(const char *stage)
{
    (void)snprintf(s_diag.recovery_stage, sizeof(s_diag.recovery_stage),
                   "%s", stage != NULL ? stage : "unknown");
}

/*
 * SIM7080 puede responder SMSTATE=0 y, aun así, rechazar SMCONN con
 * "operation not allowed" porque conserva el transporte TCP/MQTT anterior.
 * En ese estado una nueva configuracion SMCONF no limpia la FSM: se requiere
 * bajar y volver a subir el contexto PDP. El mismo error aparece durante los
 * cortes de datos de la red, donde ciclar el PDP no ayuda y cada ciclo genera
 * re-registros: se limita a uno por METER_MQTT_PDP_CYCLE_COOLDOWN_MS mientras
 * no haya sesion (el contador se reinicia al conectar).
 */
static esp_err_t mqtt_client_cycle_pdp_after_op_not_allowed(const char *origin)
{
    const int64_t now_us = esp_timer_get_time();
    const int64_t cooldown_us =
        (int64_t)MQTT_OP_NOT_ALLOWED_PDP_CYCLE_COOLDOWN_MS * 1000LL;
    if ((s_last_op_not_allowed_pdp_cycle_us != 0LL) &&
        ((now_us - s_last_op_not_allowed_pdp_cycle_us) < cooldown_us)) {
        mqtt_client_set_recovery_stage("pdp_cycle_cooldown");
        LOG_WARN(TAG,
                 "%s operation not allowed: PDP cycle suppressed by %u ms cooldown",
                 origin != NULL ? origin : "MQTT",
                 (unsigned)MQTT_OP_NOT_ALLOWED_PDP_CYCLE_COOLDOWN_MS);
        return ESP_ERR_INVALID_STATE;
    }

    const int64_t start_us = esp_timer_get_time();
    s_last_op_not_allowed_pdp_cycle_us = start_us;
    s_connected = false;
    s_configured = false;
    s_force_reconfigure = true;
    mqtt_client_set_recovery_stage("cycle_pdp_operation_not_allowed");
    LOG_WARN(TAG,
             "%s operation not allowed with SMSTATE=0; cycling PDP to clear modem MQTT/TCP state",
             origin != NULL ? origin : "MQTT");

    node_health_cellular_attempt_inc();
    const esp_err_t err = sim7080g_cycle_pdp();
    if (err == ESP_OK) {
        node_health_cellular_success_inc();
        mqtt_client_set_recovery_stage("pdp_cycle_complete");
        s_diag.cnact_state = 1;
        LOG_WARN(TAG,
                 "PDP cycle complete after %s; MQTT reconnect will retry on the next scheduled attempt",
                 origin != NULL ? origin : "MQTT error");
    } else {
        mqtt_client_set_recovery_stage("pdp_cycle_failed");
        LOG_ERROR(TAG, "PDP cycle after %s failed: %s",
                  origin != NULL ? origin : "MQTT error", esp_err_to_name(err));
    }
    s_diag.last_recovery_error = err;
    s_diag.last_recovery_ms =
        (uint32_t)((esp_timer_get_time() - start_us) / 1000LL);
    return err;
}

static void mqtt_client_mark_connected(void)
{
    s_connected = true;
    s_last_op_not_allowed_pdp_cycle_us = 0LL;
    s_smconn_pending_us = 0LL;
    node_health_mqtt_success_inc();
}

static bool mqtt_client_smconn_pending(void)
{
    return (s_smconn_pending_us != 0LL) &&
           ((esp_timer_get_time() - s_smconn_pending_us) <
            ((int64_t)METER_MQTT_SMCONN_LATE_ADOPT_MS * 1000LL));
}

static int mqtt_client_parse_registration(const char *response)
{
    const char *p = response != NULL ? strstr(response, "+CEREG:") : NULL;
    if (p == NULL) { return -1; }
    int first = -1;
    int second = -1;
    const int count = sscanf(p + strlen("+CEREG:"), " %d,%d", &first, &second);
    return count == 2 ? second : (count == 1 ? first : -1);
}

static int mqtt_client_parse_cnact(const char *response)
{
    const char *p = response != NULL ? strstr(response, "+CNACT:") : NULL;
    if (p == NULL) { return -1; }
    int cid = -1;
    int state = -1;
    return sscanf(p + strlen("+CNACT:"), " %d,%d", &cid, &state) == 2
               ? state : -1;
}

static bool mqtt_client_resp_has_op_not_allowed(const char *response)
{
    if (response == NULL) {
        return false;
    }
    return (strstr(response, "operation not allowed") != NULL);
}

static bool mqtt_client_resp_has_modem_reboot_banner(const char *response)
{
    if (response == NULL) {
        return false;
    }
    return (strstr(response, "NORMAL POWER DOWN") != NULL) ||
           (strstr(response, "\nRDY") != NULL) ||
           (strstr(response, "SMS Ready") != NULL) ||
           (strstr(response, "+CFUN: 1") != NULL);
}

static esp_err_t mqtt_client_send_cmd_with_log(const char *cmd,
                                               uint32_t timeout_ms,
                                               uint8_t retries,
                                               const char *label,
                                               bool optional)
{
    char response[256];
    response[0] = '\0';
    const esp_err_t err = sim7080g_hal_send_cmd(cmd,
                                                 NULL,
                                                 response,
                                                 sizeof(response),
                                                 timeout_ms,
                                                 retries);
    if (err != ESP_OK) {
        if (optional) {
            LOG_WARN(TAG, "%s failed (continuing). resp=%s", label, response);
        } else {
            LOG_WARN(TAG, "%s failed. resp=%s", label, response);
        }
    }
    return err;
}

static esp_err_t mqtt_client_query_state(int *state_out)
{
    char response[256];
    esp_err_t err = sim7080g_hal_send_cmd(AT_SMSTATE_QUERY,
                                          NULL,
                                          response,
                                          sizeof(response),
                                          k_mqtt_state_timeout_ms,
                                          1U);
    if (err != ESP_OK) {
        return err;
    }

    const char *prefix = strstr(response, "+SMSTATE:");
    if (prefix == NULL) {
        return ESP_FAIL;
    }
    prefix += strlen("+SMSTATE:");
    while ((*prefix == ' ') || (*prefix == ':')) {
        ++prefix;
    }

    const int value = atoi(prefix);
    if (state_out != NULL) {
        *state_out = value;
    }
    return ESP_OK;
}

static esp_err_t mqtt_client_wait_connected_window(uint32_t window_ms, uint32_t step_ms)
{
    const TickType_t start = xTaskGetTickCount();
    const TickType_t window_ticks = pdMS_TO_TICKS(window_ms);
    while ((xTaskGetTickCount() - start) < window_ticks) {
        int st = 0;
        if ((mqtt_client_query_state(&st) == ESP_OK) && (st == 1)) {
            s_connected = true;
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(step_ms));
    }
    return ESP_ERR_TIMEOUT;
}

static void mqtt_client_dump_modem_snapshot(const char *reason)
{
    char response[256];
    esp_err_t err = ESP_FAIL;

    response[0] = '\0';
    err = sim7080g_hal_send_cmd(AT_CEREG_QUERY, NULL, response, sizeof(response), k_mqtt_diag_timeout_ms, 1U);
    if (err == ESP_OK) {
        s_diag.cereg_stat = mqtt_client_parse_registration(response);
        LOG_WARN(TAG, "%s snapshot CEREG: %s", reason, response);
    } else {
        LOG_WARN(TAG, "%s snapshot CEREG failed: %s", reason, esp_err_to_name(err));
    }

    response[0] = '\0';
    err = sim7080g_hal_send_cmd(AT_CGATT_QUERY, NULL, response, sizeof(response), k_mqtt_diag_timeout_ms, 1U);
    if (err == ESP_OK) {
        LOG_WARN(TAG, "%s snapshot CGATT: %s", reason, response);
    } else {
        LOG_WARN(TAG, "%s snapshot CGATT failed: %s", reason, esp_err_to_name(err));
    }

    response[0] = '\0';
    err = sim7080g_hal_send_cmd(AT_CNACT_QUERY, NULL, response, sizeof(response), k_mqtt_diag_timeout_ms, 1U);
    if (err == ESP_OK) {
        s_diag.cnact_state = mqtt_client_parse_cnact(response);
        LOG_WARN(TAG, "%s snapshot CNACT: %s", reason, response);
    } else {
        LOG_WARN(TAG, "%s snapshot CNACT failed: %s", reason, esp_err_to_name(err));
    }

    response[0] = '\0';
    err = sim7080g_hal_send_cmd(AT_CSQ_QUERY, NULL, response, sizeof(response), k_mqtt_diag_timeout_ms, 1U);
    if (err == ESP_OK) {
        LOG_WARN(TAG, "%s snapshot CSQ: %s", reason, response);
    } else {
        LOG_WARN(TAG, "%s snapshot CSQ failed: %s", reason, esp_err_to_name(err));
    }

    int smstate = -1;
    err = mqtt_client_query_state(&smstate);
    s_diag.smstate = (err == ESP_OK) ? smstate : -1;
    LOG_WARN(TAG, "%s snapshot SMSTATE: %s%d", reason,
             err == ESP_OK ? "" : "unavailable/", s_diag.smstate);

    response[0] = '\0';
    err = sim7080g_hal_send_cmd(AT_CPSI_QUERY, NULL, response,
                                sizeof(response), 1500U, 1U);
    sim7080g_radio_t radio = {0};
    s_diag.radio_valid = (err == ESP_OK) &&
                         sim7080g_radio_parse_cpsi(response, &radio);
    if (s_diag.radio_valid) {
        s_diag.rsrp_dbm = radio.rsrp_dbm;
        s_diag.rsrq_db = radio.rsrq_db;
        s_diag.sinr_db = radio.sinr_db;
        LOG_WARN(TAG, "%s snapshot CPSI: RSRP=%.1f RSRQ=%.1f SINR=%.1f raw=%s",
                 reason, (double)radio.rsrp_dbm, (double)radio.rsrq_db,
                 (double)radio.sinr_db, response);
    } else {
        LOG_WARN(TAG, "%s snapshot CPSI unavailable: err=%s raw=%s", reason,
                 esp_err_to_name(err), response);
    }
}

static void mqtt_client_log_state(const char *prefix)
{
    int state = -1;
    const esp_err_t err = mqtt_client_query_state(&state);
    if (err == ESP_OK) {
        LOG_WARN(TAG, "%s SMSTATE=%d", prefix, state);
    } else {
        LOG_WARN(TAG, "%s SMSTATE query failed: %s", prefix, esp_err_to_name(err));
    }
}

#if (METER_MQTT_USE_TLS != 0)
/**
 * Sube la CA propia al filesystem del SIM7080G y configura el contexto SSL.
 * Se ejecuta una vez por boot: el archivo persiste en el FS del modem, pero
 * reescribirlo en cada arranque garantiza que una rotacion de CA se aplique
 * con solo reflashear el firmware.
 *
 * Secuencia (SIM7080 Series AT Command Manual, cap. CFS y CSSLCFG):
 *   CFSINIT -> CFSWFILE (raw PEM tras prompt DOWNLOAD) -> CFSTERM
 *   CSSLCFG CONVERT (PEM -> formato interno) -> CSSLCFG SSLVERSION -> SNI
 */
static esp_err_t mqtt_client_provision_tls(void)
{
    static bool s_tls_provisioned;
    if (s_tls_provisioned) {
        return ESP_OK;
    }

    if (strstr(MQTT_CA_CERT_PEM, "PEGAR-CA-AQUI") != NULL) {
        LOG_ERROR(TAG, "TLS habilitado pero mqtt_ca_cert.h tiene el placeholder. "
                       "Generar la CA (mosquitto-config/certs) y pegar ca.crt");
        return ESP_ERR_INVALID_STATE;
    }

    const size_t ca_len = strlen(MQTT_CA_CERT_PEM);
    char cmd[160];

    esp_err_t err = mqtt_client_send_cmd_with_log(AT_CFSINIT,
                                                  METER_SIM7080G_AT_TIMEOUT_MS,
                                                  1U, "CFSINIT", false);
    if (err != ESP_OK) {
        return err;
    }

    (void)snprintf(cmd, sizeof(cmd), AT_CFSWFILE_FMT,
                   METER_MQTT_TLS_CA_NAME, (unsigned)ca_len,
                   (unsigned)METER_MQTT_TLS_FSWRITE_TIMEOUT_MS);
    err = sim7080g_hal_send_cmd(cmd, "DOWNLOAD", NULL, 0U,
                                METER_SIM7080G_AT_TIMEOUT_MS, 1U);
    if (err == ESP_OK) {
        err = sim7080g_hal_send_payload(MQTT_CA_CERT_PEM, false, NULL,
                                        METER_MQTT_TLS_FSWRITE_TIMEOUT_MS);
    }
    // CFSTERM siempre, incluso tras fallo: libera el buffer del FS para
    // que el siguiente intento no reciba "operation not allowed".
    (void)mqtt_client_send_cmd_with_log(AT_CFSTERM,
                                        METER_SIM7080G_AT_TIMEOUT_MS,
                                        1U, "CFSTERM", true);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "escritura de CA al FS del modem fallo");
        return err;
    }

    (void)snprintf(cmd, sizeof(cmd), AT_CSSLCFG_CONVERT_FMT, METER_MQTT_TLS_CA_NAME);
    err = mqtt_client_send_cmd_with_log(cmd, METER_SIM7080G_AT_TIMEOUT_MS,
                                        1U, "CSSLCFG CONVERT", false);
    if (err != ESP_OK) {
        return err;
    }

    (void)snprintf(cmd, sizeof(cmd), AT_CSSLCFG_VER_FMT,
                   (unsigned)METER_MQTT_TLS_CTX, (unsigned)METER_MQTT_TLS_VERSION);
    err = mqtt_client_send_cmd_with_log(cmd, METER_SIM7080G_AT_TIMEOUT_MS,
                                        1U, "CSSLCFG SSLVERSION", false);
    if (err != ESP_OK) {
        return err;
    }

    // SNI: necesario porque el proxy de Railway enruta por hostname.
    // Opcional en firmwares de modem viejos: si no existe el comando se
    // continua (el proxy actual enruta por puerto, no por SNI).
    (void)snprintf(cmd, sizeof(cmd), AT_CSSLCFG_SNI_FMT,
                   (unsigned)METER_MQTT_TLS_CTX, METER_MQTT_HOST);
    (void)mqtt_client_send_cmd_with_log(cmd, METER_SIM7080G_AT_TIMEOUT_MS,
                                        1U, "CSSLCFG SNI", true);

    s_tls_provisioned = true;
    LOG_INFO(TAG, "TLS provisionado: CA %s, ctx %u, TLS 1.2",
             METER_MQTT_TLS_CA_NAME, (unsigned)METER_MQTT_TLS_CTX);
    return ESP_OK;
}
#endif // METER_MQTT_USE_TLS

static esp_err_t mqtt_client_configure_profile(void)
{
    int smstate = 0;
    const bool session_up =
        (mqtt_client_query_state(&smstate) == ESP_OK) && (smstate == 1);

    // Sesion abierta por un SMCONN que el firmware dio por vencido: es una
    // conexion nueva y valida. Cerrarla con SMDISC producia en el broker la
    // secuencia CONNECT -> DISCONNECT -> CONNECT. Se adopta tal cual (el
    // perfil SMCONF ya estaba aplicado cuando se envio ese SMCONN).
    if (session_up && mqtt_client_smconn_pending()) {
        LOG_INFO(TAG, "adopting MQTT session from delayed SMCONN (%lu ms after timeout)",
                 (unsigned long)((esp_timer_get_time() - s_smconn_pending_us) / 1000LL));
        s_configured = true;
        s_force_reconfigure = false;
        return ESP_OK;
    }

    // Limpia sesion previa solo si el modem reporta MQTT conectado.
    if (session_up) {
        (void)mqtt_client_send_cmd_with_log(AT_SMDISC,
                                            5000U,
                                            1U,
                                            "SMDISC pre-init",
                                            true);
        vTaskDelay(pdMS_TO_TICKS(300U));
    }

    char cmd[192];
    (void)snprintf(cmd, sizeof(cmd), AT_SMCONF_URL_FMT, METER_MQTT_HOST, (unsigned)METER_MQTT_PORT);
    esp_err_t err = mqtt_client_send_cmd_with_log(cmd,
                                                  METER_SIM7080G_AT_TIMEOUT_MS,
                                                  METER_SIM7080G_MAX_RETRIES,
                                                  "SMCONF URL",
                                                  false);
    if (err != ESP_OK) {
        return err;
    }

#if (METER_MQTT_USE_TLS != 0)
    err = mqtt_client_provision_tls();
    if (err != ESP_OK) {
        return err;
    }
    // SMSSL vincula la sesion MQTT al contexto SSL (indice = ctx + 1;
    // 0 significa "sin SSL").
    (void)snprintf(cmd, sizeof(cmd), AT_SMSSL_ON_FMT,
                   (unsigned)(METER_MQTT_TLS_CTX + 1U), METER_MQTT_TLS_CA_NAME);
    err = mqtt_client_send_cmd_with_log(cmd, METER_SIM7080G_AT_TIMEOUT_MS,
                                        1U, "SMSSL on", false);
    if (err != ESP_OK) {
        return err;
    }
#else
    // Asegura sesion en claro aunque un firmware anterior haya dejado SSL
    // activo en la NVRAM del modem. Opcional: en modems viejos no existe.
    (void)mqtt_client_send_cmd_with_log(AT_SMSSL_OFF,
                                        METER_SIM7080G_AT_TIMEOUT_MS,
                                        1U, "SMSSL off", true);
#endif

    // CLIENTID = device_id (IMEI): unico por modem, evita que dos nodos
    // con el mismo binario se expulsen mutuamente de la sesion MQTT.
    (void)snprintf(cmd, sizeof(cmd), AT_SMCONF_CLIENT_FMT, mqtt_topics_device_id());
    err = mqtt_client_send_cmd_with_log(cmd,
                                        METER_SIM7080G_AT_TIMEOUT_MS,
                                        METER_SIM7080G_MAX_RETRIES,
                                        "SMCONF CLIENTID",
                                        false);
    if (err != ESP_OK) {
        return err;
    }

    if (METER_MQTT_USERNAME[0] != '\0') {
        (void)snprintf(cmd, sizeof(cmd), AT_SMCONF_USER_FMT, METER_MQTT_USERNAME);
        err = mqtt_client_send_cmd_with_log(cmd,
                                            METER_SIM7080G_AT_TIMEOUT_MS,
                                            METER_SIM7080G_MAX_RETRIES,
                                            "SMCONF USERNAME",
                                            false);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (METER_MQTT_PASSWORD[0] != '\0') {
        (void)snprintf(cmd, sizeof(cmd), AT_SMCONF_PASS_FMT, METER_MQTT_PASSWORD);
        err = mqtt_client_send_cmd_with_log(cmd,
                                            METER_SIM7080G_AT_TIMEOUT_MS,
                                            METER_SIM7080G_MAX_RETRIES,
                                            "SMCONF PASSWORD",
                                            false);
        if (err != ESP_OK) {
            return err;
        }
    }

    (void)snprintf(cmd, sizeof(cmd), AT_SMCONF_KEEP_FMT, (unsigned)METER_MQTT_KEEPALIVE_S);
    err = mqtt_client_send_cmd_with_log(cmd,
                                        METER_SIM7080G_AT_TIMEOUT_MS,
                                        METER_SIM7080G_MAX_RETRIES,
                                        "SMCONF KEEPTIME",
                                        false);
    if (err != ESP_OK) {
        return err;
    }

    (void)mqtt_client_send_cmd_with_log(AT_SMCONF_CLEANSS,
                                        METER_SIM7080G_AT_TIMEOUT_MS,
                                        1U,
                                        "SMCONF CLEANSS",
                                        true);
    (void)mqtt_client_send_cmd_with_log(AT_SMCONF_QOS0,
                                        METER_SIM7080G_AT_TIMEOUT_MS,
                                        1U,
                                        "SMCONF QOS",
                                        true);

    s_configured = true;
    s_force_reconfigure = false;
    LOG_INFO(TAG, "MQTT profile configured (pending SMCONN)");
    return ESP_OK;
}

/**
 * @brief Inicializa perfil MQTT del modulo SIM7080G.
 * @return ESP_OK en caso de exito.
 */
esp_err_t mqtt_client_init(void)
{
    esp_err_t err = sim7080g_init();
    if (err != ESP_OK) {
        return err;
    }

    // Dar tiempo al subsistema MQTT interno del SIM7080G para inicializarse
    // despues de que el modem responda al AT basico. Sin este delay,
    // AT+SMCONF falla con "+CME ERROR: operation not allowed" incluso cuando
    // el modem ya responde a AT (el stack MQTT interno no esta listo todavia).
    vTaskDelay(pdMS_TO_TICKS(2000U));

    if (!sim7080g_is_pdp_ready()) {
        node_health_cellular_attempt_inc();
        err = sim7080g_recover_network();
        if (err != ESP_OK) {
            LOG_WARN(TAG, "network recover before MQTT init failed");
            return err;
        }
        node_health_cellular_success_inc();
    }
    return mqtt_client_configure_profile();
}

esp_err_t mqtt_client_connect(void)
{
    node_health_mqtt_attempt_inc();
    if (!s_configured || s_force_reconfigure) {
        esp_err_t err = mqtt_client_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    // Verifica que red/attach/PDP sigan activos antes de intentar SMCONN.
    esp_err_t err = sim7080g_refresh_runtime_state();
    if (err != ESP_OK) {
        LOG_WARN(TAG, "runtime network/PDP not ready before SMCONN - recovering");
        node_health_cellular_attempt_inc();
        err = sim7080g_recover_network();
        if (err != ESP_OK) {
            return err;
        }
        node_health_cellular_success_inc();
    }

    // Evita SMCONN redundante cuando el modem ya reporta sesion activa.
    int pre_state = 0;
    if ((mqtt_client_query_state(&pre_state) == ESP_OK) && (pre_state == 1)) {
        mqtt_client_mark_connected();
        LOG_INFO(TAG, "MQTT already connected (SMSTATE=1), skipping SMCONN");
        return ESP_OK;
    }

    // Cronometro del establecimiento de sesion: con METER_MQTT_USE_TLS la
    // diferencia respecto a la conexion en claro es el costo del handshake
    // TLS sobre LTE-M (dato para el capitulo de ciberseguridad).
    const int64_t connect_t0_us = esp_timer_get_time();

    char response[256];
    response[0] = '\0';
    err = sim7080g_hal_send_cmd(AT_SMCONN,
                                NULL,
                                response,
                                sizeof(response),
                                MQTT_SMCONN_TIMEOUT_MS,
                                MQTT_SMCONN_RETRIES);
    if (err != ESP_OK) {
        const bool smconn_timed_out = (err == ESP_ERR_TIMEOUT);
        int state_after_fail = 0;
        const esp_err_t state_err = mqtt_client_query_state(&state_after_fail);
        if ((state_err == ESP_OK) && (state_after_fail == 1)) {
            mqtt_client_mark_connected();
            LOG_INFO(TAG,
                     "SMCONN devolvio error pero la sesion ya estaba activa (SMSTATE=1). resp=%s",
                     response);
            return ESP_OK;
        }

        LOG_WARN(TAG,
                 "SMCONN failed. resp=%s state_err=%s state=%d",
                 response,
                 esp_err_to_name(state_err),
                 (state_err == ESP_OK) ? state_after_fail : -1);
        if (!smconn_timed_out) {
            mqtt_client_dump_modem_snapshot("SMCONN fail");
        }

        if (mqtt_client_resp_has_modem_reboot_banner(response)) {
            LOG_WARN(TAG, "modem reboot banner detected during SMCONN; forcing modem re-probe");
            sim7080g_mark_modem_unready();
            s_connected = false;
            s_configured = false;
            return ESP_ERR_INVALID_STATE;
        }

        if (smconn_timed_out &&
            (mqtt_client_wait_connected_window(METER_MQTT_SMCONN_POSTWAIT_MS, 1000U) == ESP_OK)) {
            mqtt_client_mark_connected();
            s_last_connect_ms = (uint32_t)((esp_timer_get_time() - connect_t0_us) / 1000LL);
            LOG_INFO(TAG, "MQTT connected after delayed SMCONN completion (%lu ms, TLS=%d)",
                     (unsigned long)s_last_connect_ms, (int)(METER_MQTT_USE_TLS != 0));
            return ESP_OK;
        }

        if (mqtt_client_resp_has_op_not_allowed(response)) {
            const esp_err_t cycle_err =
                mqtt_client_cycle_pdp_after_op_not_allowed("SMCONN");
            if ((cycle_err != ESP_OK) && (cycle_err != ESP_ERR_INVALID_STATE)) {
                LOG_WARN(TAG, "SMCONN remains blocked; PDP recovery failed: %s",
                         esp_err_to_name(cycle_err));
            }
            return err;
        }

        if (smconn_timed_out) {
            s_connected = false;
            s_smconn_pending_us = esp_timer_get_time();
            return err;
        }

        vTaskDelay(pdMS_TO_TICKS(500U));

        response[0] = '\0';
        err = sim7080g_hal_send_cmd(AT_SMCONN,
                                    NULL,
                                    response,
                                    sizeof(response),
                                    MQTT_SMCONN_TIMEOUT_MS,
                                    1U);
        if (err != ESP_OK) {
            if (mqtt_client_resp_has_modem_reboot_banner(response)) {
                LOG_WARN(TAG, "modem reboot banner detected during SMCONN retry; forcing modem re-probe");
                sim7080g_mark_modem_unready();
                s_connected = false;
                s_configured = false;
                return ESP_ERR_INVALID_STATE;
            }
            s_connected = false;
            if (err == ESP_ERR_TIMEOUT) {
                s_smconn_pending_us = esp_timer_get_time();
            }
            LOG_WARN(TAG, "SMCONN retry failed. resp=%s", response);
            if (err != ESP_ERR_TIMEOUT) {
                mqtt_client_dump_modem_snapshot("SMCONN retry fail");
                mqtt_client_log_state("after SMCONN retry fail:");
            }
            if (mqtt_client_resp_has_op_not_allowed(response)) {
                const esp_err_t cycle_err =
                    mqtt_client_cycle_pdp_after_op_not_allowed("SMCONN retry");
                if ((cycle_err != ESP_OK) &&
                    (cycle_err != ESP_ERR_INVALID_STATE)) {
                    LOG_WARN(TAG,
                             "SMCONN retry remains blocked; PDP recovery failed: %s",
                             esp_err_to_name(cycle_err));
                }
            }
            return err;
        }
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    int state = 0;
    err = mqtt_client_query_state(&state);
    if ((err == ESP_OK) && (state == 1)) {
        mqtt_client_mark_connected();
        s_last_connect_ms = (uint32_t)((esp_timer_get_time() - connect_t0_us) / 1000LL);
        LOG_INFO(TAG, "MQTT connected (%lu ms, TLS=%d)",
                 (unsigned long)s_last_connect_ms, (int)(METER_MQTT_USE_TLS != 0));
        return ESP_OK;
    }

    s_connected = false;
    s_configured = false;
    s_force_reconfigure = true;
    LOG_WARN(TAG, "SMCONN returned but state is not connected (state=%d)", state);
    return ESP_FAIL;
}

esp_err_t mqtt_client_disconnect(void)
{
    if (!s_connected) {
        return ESP_OK;
    }

    const esp_err_t err = sim7080g_hal_send_cmd(AT_SMDISC,
                                                 NULL,
                                                 NULL,
                                                 0U,
                                                 k_mqtt_state_timeout_ms,
                                                 1U);
    s_connected = false;
    return err;
}

esp_err_t mqtt_client_publish(const char *topic, const char *payload, int qos, int retain)
{
    if ((topic == NULL) || (payload == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_connected) {
        s_diag.last_publish_error = ESP_ERR_INVALID_STATE;
        mqtt_client_set_recovery_stage("publish_no_session");
        mqtt_client_dump_modem_snapshot("publish without MQTT session");
        return ESP_ERR_INVALID_STATE;
    }

    // Probe AT rapido antes de SMPUB para no gastar 45s en retries
    // contra un modem muerto o desconectado.
    esp_err_t probe_err = sim7080g_hal_send_cmd("AT",
                                                 NULL,
                                                 NULL,
                                                 0U,
                                                 2000U,
                                                 1U);
    if (probe_err != ESP_OK) {
        LOG_WARN(TAG, "pre-publish AT probe failed - modem not responding");
        s_connected = false;
        s_diag.last_publish_error = probe_err;
        mqtt_client_set_recovery_stage("publish_at_probe");
        mqtt_client_dump_modem_snapshot("pre-publish AT probe fail");
        return ESP_ERR_TIMEOUT;
    }

    const size_t payload_bytes = strlen(payload) + 2U;
    if (payload_bytes > METER_MQTT_SMPUB_SAFE_MAX_BYTES) {
        LOG_ERROR(TAG,
                  "SMPUB payload too large: %u bytes (safe max=%u)",
                  (unsigned)payload_bytes,
                  (unsigned)METER_MQTT_SMPUB_SAFE_MAX_BYTES);
        s_diag.last_publish_error = ESP_ERR_INVALID_SIZE;
        mqtt_client_set_recovery_stage("publish_oversize");
        mqtt_client_dump_modem_snapshot("SMPUB payload oversize");
        return ESP_ERR_INVALID_SIZE;
    }
    const int payload_len = (int)payload_bytes;
    char cmd[256];
    (void)snprintf(cmd, sizeof(cmd), AT_SMPUB_FMT, topic, payload_len, qos, retain);

    char response[192] = {0};
    esp_err_t err = sim7080g_hal_send_cmd(cmd,
                                          ">",
                                          response,
                                          sizeof(response),
                                          METER_MQTT_PUBLISH_TIMEOUT_MS,
                                          METER_SIM7080G_MAX_RETRIES);
    if (err != ESP_OK) {
        LOG_WARN(TAG,
                 "SMPUB rejected topic=%s bytes=%d response=%s",
                 topic,
                 payload_len,
                 response[0] != '\0' ? response : "<empty>");
        s_connected = false;
        s_diag.last_publish_error = err;
        mqtt_client_set_recovery_stage("publish_command");
        mqtt_client_dump_modem_snapshot("SMPUB command fail");
        return err;
    }

    err = sim7080g_hal_send_payload(payload, true, NULL, METER_MQTT_PUBLISH_TIMEOUT_MS);
    if (err != ESP_OK) {
        s_connected = false;
        s_diag.last_publish_error = err;
        mqtt_client_set_recovery_stage("publish_payload");
        mqtt_client_dump_modem_snapshot("SMPUB payload fail");
        return err;
    }
    // Para QoS 1, el OK del comando AT no basta si el modem perdio la sesion
    // durante la publicacion. Conservar el registro para reintento ante duda.
    if (qos > 0) {
        int state = 0;
        const esp_err_t state_err = mqtt_client_query_state(&state);
        s_diag.smstate = (state_err == ESP_OK) ? state : -1;
        if ((state_err != ESP_OK) || (state != 1)) {
            s_connected = false;
            s_diag.last_publish_error =
                (state_err != ESP_OK) ? state_err : ESP_ERR_INVALID_STATE;
            mqtt_client_set_recovery_stage("publish_session_unconfirmed");
            mqtt_client_dump_modem_snapshot("SMPUB session not confirmed");
            return s_diag.last_publish_error;
        }
    }
    s_diag.last_publish_error = ESP_OK;
    return ESP_OK;
}

bool mqtt_client_is_connected(void)
{
    return s_connected;
}

uint32_t mqtt_client_get_last_connect_ms(void)
{
    return s_last_connect_ms;
}

void mqtt_client_get_diagnostics(mqtt_diagnostics_t *out)
{
    if (out != NULL) {
        *out = s_diag;
    }
}

bool mqtt_client_check_connected(void)
{
    int state = 0;
    const esp_err_t err = mqtt_client_query_state(&state);
    if (err != ESP_OK) {
        s_connected = false;
        return false;
    }
    s_connected = (state == 1);
    return s_connected;
}

esp_err_t mqtt_client_recover(void)
{
    const int64_t recovery_t0_us = esp_timer_get_time();
    s_connected = false;
    mqtt_client_set_recovery_stage("disconnect_mqtt");
    mqtt_client_dump_modem_snapshot("recovery start");

    // Intenta SMDISC para limpiar la sesion MQTT interna del SIM7080G.
    // Si falla con "operation not allowed" el estado TCP subyacente esta
    // atascado: ciclar el contexto PDP es la unica forma de liberarlo.
    char smdisc_resp[64];
    smdisc_resp[0] = '\0';
    const esp_err_t disc_err = sim7080g_hal_send_cmd(AT_SMDISC,
                                                     NULL,
                                                     smdisc_resp,
                                                     sizeof(smdisc_resp),
                                                     1500U,
                                                     0U);
    if (disc_err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(300U));
    } else if (mqtt_client_resp_has_op_not_allowed(smdisc_resp)) {
        const esp_err_t cycle_err =
            mqtt_client_cycle_pdp_after_op_not_allowed("SMDISC");
        if ((cycle_err != ESP_OK) && (cycle_err != ESP_ERR_INVALID_STATE)) {
            LOG_WARN(TAG, "SMDISC recovery could not cycle PDP: %s",
                     esp_err_to_name(cycle_err));
        }
    }

    // Fuerza reprovisionar perfil MQTT despues de cualquier fallo.
    s_configured = false;
    s_force_reconfigure = true;

    node_health_cellular_attempt_inc();
    mqtt_client_set_recovery_stage("recover_network");
    esp_err_t err = sim7080g_recover_network();
    if (err != ESP_OK) {
        s_diag.last_recovery_error = err;
        s_diag.last_recovery_ms = (uint32_t)((esp_timer_get_time() - recovery_t0_us) / 1000LL);
        mqtt_client_set_recovery_stage("network_failed");
        mqtt_client_dump_modem_snapshot("network recovery fail");
        return err;
    }
    node_health_cellular_success_inc();

    mqtt_client_set_recovery_stage("configure_mqtt");
    err = mqtt_client_configure_profile();
    if (err != ESP_OK) {
        s_diag.last_recovery_error = err;
        s_diag.last_recovery_ms = (uint32_t)((esp_timer_get_time() - recovery_t0_us) / 1000LL);
        mqtt_client_set_recovery_stage("profile_failed");
        mqtt_client_dump_modem_snapshot("MQTT profile recovery fail");
        return err;
    }
    mqtt_client_set_recovery_stage("connect_mqtt");
    err = mqtt_client_connect();
    s_diag.last_recovery_error = err;
    s_diag.last_recovery_ms = (uint32_t)((esp_timer_get_time() - recovery_t0_us) / 1000LL);
    mqtt_client_set_recovery_stage(err == ESP_OK ? "complete" : "connect_failed");
    LOG_INFO(TAG, "MQTT recovery stage=%s duration=%lu ms result=%s",
             s_diag.recovery_stage, (unsigned long)s_diag.last_recovery_ms,
             esp_err_to_name(err));
    if (err != ESP_OK) {
        mqtt_client_dump_modem_snapshot("MQTT reconnect fail");
    }
    return err;
}

esp_err_t mqtt_client_subscribe(const char *topic)
{
    if (topic == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    char cmd[160];
    (void)snprintf(cmd, sizeof(cmd), "AT+SMSUB=\"%s\",1", topic);
    char response[128];
    response[0] = '\0';
    esp_err_t err = sim7080g_hal_send_cmd(cmd, NULL, response, sizeof(response),
                                           k_mqtt_state_timeout_ms, 1U);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "SMSUB failed for %s: %s", topic, esp_err_to_name(err));
    } else {
        LOG_INFO(TAG, "subscribed to %s", topic);
    }
    return err;
}

esp_err_t mqtt_client_read_command(char *cmd_out, size_t cmd_len)
{
    if ((cmd_out == NULL) || (cmd_len == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    // URC format: +SMSUB: "topic","payload"
    char urc[256];
    esp_err_t err = sim7080g_hal_read_urc("+SMSUB:", urc, sizeof(urc), 200U);
    if (err != ESP_OK) {
        return err;
    }

    // Parse payload from URC: +SMSUB: "topic","payload"
    // Find second quoted string
    const char *first_q = strchr(urc, '"');
    if (first_q == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    const char *end_first = strchr(first_q + 1, '"');
    if (end_first == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    const char *second_q = strchr(end_first + 1, '"');
    if (second_q == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    const char *end_second = strchr(second_q + 1, '"');
    if (end_second == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    size_t payload_len = (size_t)(end_second - second_q - 1);
    if (payload_len >= cmd_len) {
        payload_len = cmd_len - 1U;
    }
    (void)memcpy(cmd_out, second_q + 1, payload_len);
    cmd_out[payload_len] = '\0';

    LOG_INFO(TAG, "command received: %s", cmd_out);
    return ESP_OK;
}



