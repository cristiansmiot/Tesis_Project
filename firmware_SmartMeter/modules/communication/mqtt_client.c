#include "mqtt_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "meter_config.h"
#include "node_health.h"
#include "sim7080g_hal.h"
#include "sim7080g_init.h"
#include "logger.h"

static const char *TAG = "mqtt_client";
static bool s_configured;
static bool s_connected;
static bool s_force_reconfigure;
static bool s_skip_lwt_config;
static const uint32_t k_mqtt_state_timeout_ms = 1000U;
static const uint32_t k_mqtt_diag_timeout_ms = 1000U;

#define AT_SMCONF_URL_FMT       "AT+SMCONF=\"URL\",\"%s\",%u"
#define AT_SMCONF_CLIENT_FMT    "AT+SMCONF=\"CLIENTID\",\"%s\""
#define AT_SMCONF_USER_FMT      "AT+SMCONF=\"USERNAME\",\"%s\""
#define AT_SMCONF_PASS_FMT      "AT+SMCONF=\"PASSWORD\",\"%s\""
#define AT_SMCONF_KEEP_FMT      "AT+SMCONF=\"KEEPTIME\",%u"
#define AT_SMCONF_CLEANSS       "AT+SMCONF=\"CLEANSS\",1"
#define AT_SMCONF_QOS0          "AT+SMCONF=\"QOS\",0"
// LWT (Last Will and Testament): el broker publica "offline" en /conexion
// automaticamente si el nodo pierde la conexion sin AT+SMDISC explicito.
// La palabra "offline" no contiene comillas internas, evitando problemas
// de escape en el parametro AT+SMCONF="MESSAGEWILL".
#define AT_SMCONF_TOPICWILL_FMT "AT+SMCONF=\"TOPICWILL\",\"%s\""
#define AT_SMCONF_MSGWILL       "AT+SMCONF=\"MESSAGEWILL\",\"offline\""
#define AT_SMCONF_QOSWILL       "AT+SMCONF=\"QOSWILL\",1"
#define AT_SMCONF_RETAINWILL    "AT+SMCONF=\"RETAINWILL\",1"
#define AT_SMCONN               "AT+SMCONN"
#define AT_SMDISC               "AT+SMDISC"
#define AT_SMSTATE_QUERY        "AT+SMSTATE?"
#define AT_SMPUB_FMT            "AT+SMPUB=\"%s\",%d,%d,%d"
#define AT_CEREG_QUERY          "AT+CEREG?"
#define AT_CGATT_QUERY          "AT+CGATT?"
#define AT_CNACT_QUERY          "AT+CNACT?"
#define AT_CSQ_QUERY            "AT+CSQ"
#define MQTT_SMCONN_TIMEOUT_MS  METER_MQTT_SMCONN_TIMEOUT_MS
#define MQTT_SMCONN_RETRIES     METER_MQTT_SMCONN_RETRIES

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

static esp_err_t mqtt_client_send_lwt_cmd(const char *cmd, const char *label)
{
    if (s_skip_lwt_config) {
        return ESP_OK;
    }

    char response[256];
    response[0] = '\0';
    const esp_err_t err = sim7080g_hal_send_cmd(cmd,
                                                NULL,
                                                response,
                                                sizeof(response),
                                                METER_SIM7080G_AT_TIMEOUT_MS,
                                                1U);
    if (err == ESP_OK) {
        return ESP_OK;
    }

    if (mqtt_client_resp_has_op_not_allowed(response)) {
        s_skip_lwt_config = true;
        LOG_WARN(TAG,
                 "%s not supported by modem - disabling future LWT config. resp=%s",
                 label,
                 response);
        return ESP_OK;
    }

    LOG_WARN(TAG, "%s failed (continuing). resp=%s", label, response);
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

static esp_err_t mqtt_client_configure_profile(void)
{
    // Limpia sesion previa solo si el modem reporta MQTT conectado.
    int smstate = 0;
    if ((mqtt_client_query_state(&smstate) == ESP_OK) && (smstate == 1)) {
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

    (void)snprintf(cmd, sizeof(cmd), AT_SMCONF_CLIENT_FMT, METER_MQTT_CLIENT_ID);
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

        // LWT es opcional. Si el firmware del modem responde
    // "operation not allowed", se desactiva para futuros reprovisionamientos
    // y no volvemos a penalizar la reconexion.
    if (!s_skip_lwt_config) {
        char lwt_cmd[128];
        (void)snprintf(lwt_cmd, sizeof(lwt_cmd),
                       AT_SMCONF_TOPICWILL_FMT, METER_MQTT_LWT_TOPIC);
        (void)mqtt_client_send_lwt_cmd(lwt_cmd, "SMCONF TOPICWILL");
        (void)mqtt_client_send_lwt_cmd(AT_SMCONF_MSGWILL, "SMCONF MESSAGEWILL");
        (void)mqtt_client_send_lwt_cmd(AT_SMCONF_QOSWILL, "SMCONF QOSWILL");
        (void)mqtt_client_send_lwt_cmd(AT_SMCONF_RETAINWILL, "SMCONF RETAINWILL");
    }

    s_configured = true;
    s_force_reconfigure = false;
    LOG_INFO(TAG, "MQTT profile configured (pending SMCONN)");
    return ESP_OK;
}

/**
 * @brief Inicializa perfil MQTT del modulo SIM7080G.
 * @param void Sin parametros.
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
        s_connected = true;
        node_health_mqtt_success_inc();
        LOG_INFO(TAG, "MQTT already connected (SMSTATE=1), skipping SMCONN");
        return ESP_OK;
    }

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
            s_connected = true;
            node_health_mqtt_success_inc();
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
            node_health_mqtt_success_inc();
            LOG_INFO(TAG, "MQTT connected after delayed SMCONN completion");
            return ESP_OK;
        }

        if (mqtt_client_resp_has_op_not_allowed(response)) {
            LOG_WARN(TAG, "SMCONN blocked by modem state (operation not allowed) - forcing MQTT reprovision");
            s_connected = false;
            s_configured = false;
            s_force_reconfigure = true;
            return err;
        }

        if (smconn_timed_out) {
            s_connected = false;
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
            LOG_WARN(TAG, "SMCONN retry failed. resp=%s", response);
            if (err != ESP_ERR_TIMEOUT) {
                mqtt_client_dump_modem_snapshot("SMCONN retry fail");
                mqtt_client_log_state("after SMCONN retry fail:");
            }
            if (mqtt_client_resp_has_op_not_allowed(response)) {
                s_configured = false;
                s_force_reconfigure = true;
            }
            return err;
        }
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    int state = 0;
    err = mqtt_client_query_state(&state);
    if ((err == ESP_OK) && (state == 1)) {
        s_connected = true;
        node_health_mqtt_success_inc();
        LOG_INFO(TAG, "MQTT connected");
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
        return ESP_ERR_TIMEOUT;
    }

    const int payload_len = (int)strlen(payload) + 2;
    char cmd[256];
    (void)snprintf(cmd, sizeof(cmd), AT_SMPUB_FMT, topic, payload_len, qos, retain);

    esp_err_t err = sim7080g_hal_send_cmd(cmd,
                                          ">",
                                          NULL,
                                          0U,
                                          METER_MQTT_PUBLISH_TIMEOUT_MS,
                                          METER_SIM7080G_MAX_RETRIES);
    if (err != ESP_OK) {
        s_connected = false;
        return err;
    }

    err = sim7080g_hal_send_payload(payload, true, NULL, METER_MQTT_PUBLISH_TIMEOUT_MS);
    if (err != ESP_OK) {
        s_connected = false;
        return err;
    }
    return ESP_OK;
}

bool mqtt_client_is_connected(void)
{
    return s_connected;
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
    s_connected = false;

    // Siempre intenta SMDISC para limpiar sesion MQTT interna del SIM7080G.
    // Sin esto, SMCONN puede fallar con ERROR despues de un power-cycle
    // del modem aunque la red este OK (CEREG=5, CGATT=1, CNACT activo).
    // Esperar 1s despues de SMDISC para que el modem libere el estado
    // interno y permita reconfigurar LWT (TOPICWILL/MESSAGEWILL).
    const esp_err_t disc_err = mqtt_client_send_cmd_with_log(AT_SMDISC,
                                                             1500U,
                                                             0U,
                                                             "SMDISC recovery",
                                                             true);
    if (disc_err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(300U));
    }

    // Fuerza reprovisionar perfil MQTT despues de cualquier fallo.
    s_configured = false;
    s_force_reconfigure = true;

    node_health_cellular_attempt_inc();
    esp_err_t err = sim7080g_recover_network();
    if (err != ESP_OK) {
        return err;
    }
    node_health_cellular_success_inc();

    err = mqtt_client_configure_profile();
    if (err != ESP_OK) {
        return err;
    }
    return mqtt_client_connect();
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



