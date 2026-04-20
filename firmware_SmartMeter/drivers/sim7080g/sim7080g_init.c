#include "sim7080g_init.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "meter_config.h"
#include "sim7080g_hal.h"
#include "logger.h"

static const char *TAG = "sim7080g_init";
static uint8_t s_state;
static uint32_t s_detected_baudrate;
static uint8_t s_health_fail_streak;
static char s_runtime_apn[64];
static const uint32_t k_probe_at_timeout_ms = 1500U;
static const uint32_t k_network_query_timeout_ms = 1500U;
static const uint32_t k_runtime_query_timeout_ms = 1000U;
static const uint32_t k_sim_ready_timeout_ms = 30000U;
static const uint8_t k_health_fail_force_reprobe = 1U;

#define AT_BASIC            "AT"
#define AT_ECHO_OFF         "ATE0"
#define AT_CMEE_ON          "AT+CMEE=2"
#define AT_CPIN_QUERY       "AT+CPIN?"
#define AT_CSQ_QUERY        "AT+CSQ"
#define AT_CNMP_FMT         "AT+CNMP=%u"
#define AT_CMNB_FMT         "AT+CMNB=%u"
#define AT_IFC_NONE         "AT+IFC=0,0"
#define AT_CSCLK_OFF        "AT+CSCLK=0"
#define AT_CPSMS_OFF        "AT+CPSMS=0"
#define AT_CEDRXS_OFF       "AT+CEDRXS=0"
#define AT_CEREG_QUERY      "AT+CEREG?"
#define AT_CGREG_QUERY      "AT+CGREG?"
#define AT_CGATT_QUERY      "AT+CGATT?"
#define AT_CGATT_ATTACH     "AT+CGATT=1"
#define AT_CGDCONT_FMT      "AT+CGDCONT=%u,\"IP\",\"%s\""
#define AT_CNCFG_FMT        "AT+CNCFG=%u,1,\"%s\""
#define AT_CNACT_ON_FMT     "AT+CNACT=%u,1"
#define AT_CNACT_OFF_FMT    "AT+CNACT=%u,0"
#define AT_CNACT_QUERY      "AT+CNACT?"
#define AT_CGNAPN_QUERY     "AT+CGNAPN"
#define AT_CPSI_QUERY       "AT+CPSI?"
#define AT_CEER_QUERY       "AT+CEER"

static const uint32_t k_sim7080g_baud_candidates[] = {
    SIM7080G_BAUD_RATE,
#if (METER_SIM7080G_BAUD_AUTOPROBE != 0)
    115200U,
    9600U,
    57600U,
    38400U,
    19200U
#endif
};

static bool sim7080g_response_reg_ready(const char *response, const char *prefix)
{
    if ((response == NULL) || (prefix == NULL)) {
        return false;
    }
    const char *line = strstr(response, prefix);
    if (line == NULL) {
        return false;
    }
    const char *comma = strchr(line, ',');
    if (comma == NULL) {
        return false;
    }
    const int stat = atoi(comma + 1);
    return (stat == 1) || (stat == 5);
}

static const char *sim7080g_select_apn(void)
{
    return (s_runtime_apn[0] != '\0') ? s_runtime_apn : METER_SIM7080G_APN;
}

static esp_err_t sim7080g_query_network_registration(char *response,
                                                     size_t response_len,
                                                     const char **source_out)
{
    static const struct {
        const char *cmd;
        const char *prefix;
        const char *label;
    } k_queries[] = {
        { AT_CEREG_QUERY, "+CEREG:", "CEREG" },
        { AT_CGREG_QUERY, "+CGREG:", "CGREG" }
    };

    esp_err_t last_err = ESP_FAIL;
    bool got_reply = false;

    for (size_t i = 0U; i < (sizeof(k_queries) / sizeof(k_queries[0])); ++i) {
        if ((response != NULL) && (response_len > 0U)) {
            response[0] = '\0';
        }

        const esp_err_t err = sim7080g_hal_send_cmd(k_queries[i].cmd,
                                                    NULL,
                                                    response,
                                                    response_len,
                                                    k_network_query_timeout_ms,
                                                    1U);
        if (err != ESP_OK) {
            last_err = err;
            continue;
        }

        got_reply = true;
        if (sim7080g_response_reg_ready(response, k_queries[i].prefix)) {
            if (source_out != NULL) {
                *source_out = k_queries[i].label;
            }
            return ESP_OK;
        }
        last_err = ESP_ERR_INVALID_STATE;
    }

    return got_reply ? ESP_ERR_INVALID_STATE : last_err;
}

static esp_err_t sim7080g_wait_sim_ready(uint32_t timeout_ms)
{
    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    char response[256];
    char last_response[256];
    esp_err_t last_err = ESP_ERR_TIMEOUT;

    last_response[0] = '\0';
    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        response[0] = '\0';
        last_err = sim7080g_hal_send_cmd(AT_CPIN_QUERY,
                                         NULL,
                                         response,
                                         sizeof(response),
                                         METER_SIM7080G_AT_TIMEOUT_MS,
                                         1U);
        if ((last_err == ESP_OK) && (strstr(response, "+CPIN: READY") != NULL)) {
            return ESP_OK;
        }

        if (response[0] != '\0') {
            (void)strncpy(last_response, response, sizeof(last_response) - 1U);
            last_response[sizeof(last_response) - 1U] = '\0';
        }
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }

    if (last_response[0] != '\0') {
        LOG_WARN(TAG, "SIM not ready after wait. last_resp=%s", last_response);
    } else {
        LOG_WARN(TAG, "SIM not ready after wait. last_err=%s", esp_err_to_name(last_err));
    }
    return (last_err == ESP_OK) ? ESP_FAIL : last_err;
}

static bool sim7080g_response_pdp_ready_for_cid(const char *response, uint8_t cid)
{
    if (response == NULL) {
        return false;
    }

    char needle_spaced[32];
    char needle_compact[32];
    (void)snprintf(needle_spaced, sizeof(needle_spaced), "+CNACT: %u,1", (unsigned)cid);
    (void)snprintf(needle_compact, sizeof(needle_compact), "+CNACT:%u,1", (unsigned)cid);
    return (strstr(response, needle_spaced) != NULL) || (strstr(response, needle_compact) != NULL);
}

static bool sim7080g_response_attached(const char *response)
{
    if (response == NULL) {
        return false;
    }
    return (strstr(response, "+CGATT: 1") != NULL) ||
           (strstr(response, "+CGATT:1") != NULL);
}

static void sim7080g_log_ceer(void)
{
    char response[256];
    response[0] = '\0';
    const esp_err_t err = sim7080g_hal_send_cmd(AT_CEER_QUERY,
                                                NULL,
                                                response,
                                                sizeof(response),
                                                METER_SIM7080G_AT_TIMEOUT_MS,
                                                1U);
    if ((err == ESP_OK) && (response[0] != '\0')) {
        LOG_WARN(TAG, "CEER=%s", response);
    } else if (err == ESP_OK) {
        LOG_WARN(TAG, "CEER query ok (empty response)");
    } else {
        LOG_WARN(TAG, "CEER query failed: %s", esp_err_to_name(err));
    }
}

static esp_err_t sim7080g_refresh_runtime_apn(void)
{
#if (METER_SIM7080G_AUTO_APN_QUERY == 0)
    return ESP_OK;
#else
    char response[256];
    response[0] = '\0';

    const esp_err_t err = sim7080g_hal_send_cmd(AT_CGNAPN_QUERY,
                                                NULL,
                                                response,
                                                sizeof(response),
                                                METER_SIM7080G_AT_TIMEOUT_MS,
                                                1U);
    if (err != ESP_OK) {
        return err;
    }

    const char *first_quote = strchr(response, '"');
    if (first_quote == NULL) {
        return ESP_FAIL;
    }
    const char *second_quote = strchr(first_quote + 1, '"');
    if (second_quote == NULL) {
        return ESP_FAIL;
    }

    const size_t apn_len = (size_t)(second_quote - first_quote - 1);
    if ((apn_len == 0U) || (apn_len >= sizeof(s_runtime_apn))) {
        return ESP_FAIL;
    }

    char detected_apn[sizeof(s_runtime_apn)];
    (void)memcpy(detected_apn, first_quote + 1, apn_len);
    detected_apn[apn_len] = '\0';

    if (strcmp(s_runtime_apn, detected_apn) != 0) {
        (void)strncpy(s_runtime_apn, detected_apn, sizeof(s_runtime_apn) - 1U);
        s_runtime_apn[sizeof(s_runtime_apn) - 1U] = '\0';
        LOG_INFO(TAG, "APN detectado por red: %s", s_runtime_apn);
    }

    return ESP_OK;
#endif
}

static void sim7080g_log_radio_info(void)
{
    char response[256];
    response[0] = '\0';
    const esp_err_t err = sim7080g_hal_send_cmd(AT_CPSI_QUERY,
                                                NULL,
                                                response,
                                                sizeof(response),
                                                1500U,
                                                1U);
    if ((err == ESP_OK) && (response[0] != '\0')) {
        LOG_INFO(TAG, "CPSI=%s", response);
    }
}

static esp_err_t sim7080g_ensure_ps_attach(void)
{
    char response[256];
    esp_err_t err = sim7080g_hal_send_cmd(AT_CGATT_QUERY,
                                          NULL,
                                          response,
                                          sizeof(response),
                                          METER_SIM7080G_AT_TIMEOUT_MS,
                                          METER_SIM7080G_MAX_RETRIES);
    if ((err == ESP_OK) && sim7080g_response_attached(response)) {
        return ESP_OK;
    }

    LOG_WARN(TAG, "PS not attached, trying attach (CGATT=1). resp=%s", response);
    err = sim7080g_hal_send_cmd(AT_CGATT_ATTACH,
                                NULL,
                                response,
                                sizeof(response),
                                15000U,
                                METER_SIM7080G_MAX_RETRIES);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "CGATT attach failed. resp=%s", response);
        return err;
    }

    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(30000U);
    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        response[0] = '\0';
        err = sim7080g_hal_send_cmd(AT_CGATT_QUERY,
                                    NULL,
                                    response,
                                    sizeof(response),
                                    METER_SIM7080G_AT_TIMEOUT_MS,
                                    1U);
        if ((err == ESP_OK) && sim7080g_response_attached(response)) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(2000U));
    }

    LOG_WARN(TAG, "CGATT did not reach attached state. last_resp=%s", response);
    return ESP_ERR_TIMEOUT;
}

static esp_err_t sim7080g_activate_pdp_cid(uint8_t cid)
{
    char cmd[96];
    char response[256];

    // Evita reactivar PDP si ya esta arriba.
    response[0] = '\0';
    esp_err_t err = sim7080g_hal_send_cmd(AT_CNACT_QUERY,
                                          NULL,
                                          response,
                                          sizeof(response),
                                          METER_SIM7080G_AT_TIMEOUT_MS,
                                          1U);
    if ((err == ESP_OK) && sim7080g_response_pdp_ready_for_cid(response, cid)) {
        return ESP_OK;
    }

    // CGDCONT usa contexto PDP (1..16), distinto del perfil usado por CNACT/CNCFG.
    (void)snprintf(cmd,
                   sizeof(cmd),
                   AT_CGDCONT_FMT,
                   (unsigned)METER_SIM7080G_PDP_CONTEXT_ID,
                   sim7080g_select_apn());
    response[0] = '\0';
    err = sim7080g_hal_send_cmd(cmd,
                                NULL,
                                response,
                                sizeof(response),
                                METER_SIM7080G_AT_TIMEOUT_MS,
                                METER_SIM7080G_MAX_RETRIES);
    if (err != ESP_OK) {
        LOG_WARN(TAG,
                 "CGDCONT context_id=%u failed. resp=%s",
                 (unsigned)METER_SIM7080G_PDP_CONTEXT_ID,
                 response);
        return err;
    }

    (void)snprintf(cmd, sizeof(cmd), AT_CNCFG_FMT, (unsigned)cid, sim7080g_select_apn());
    response[0] = '\0';
    err = sim7080g_hal_send_cmd(cmd,
                                NULL,
                                response,
                                sizeof(response),
                                METER_SIM7080G_AT_TIMEOUT_MS,
                                METER_SIM7080G_MAX_RETRIES);
    if (err != ESP_OK) {
        // Algunos firmwares no requieren CNCFG y pueden responder ERROR.
        LOG_WARN(TAG, "CNCFG cid=%u failed (continuing). resp=%s", (unsigned)cid, response);
    }

    (void)snprintf(cmd, sizeof(cmd), AT_CNACT_ON_FMT, (unsigned)cid);
    response[0] = '\0';
    err = sim7080g_hal_send_cmd(cmd,
                                NULL,
                                response,
                                sizeof(response),
                                15000U,
                                METER_SIM7080G_MAX_RETRIES);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "CNACT cid=%u failed. resp=%s", (unsigned)cid, response);

        // Si el contexto ya esta activo, algunos firmwares devuelven ERROR en CNACT=<cid>,1.
        response[0] = '\0';
        const esp_err_t q_err = sim7080g_hal_send_cmd(AT_CNACT_QUERY,
                                                      NULL,
                                                      response,
                                                      sizeof(response),
                                                      METER_SIM7080G_AT_TIMEOUT_MS,
                                                      METER_SIM7080G_MAX_RETRIES);
        if ((q_err == ESP_OK) && sim7080g_response_pdp_ready_for_cid(response, cid)) {
            LOG_WARN(TAG, "CNACT already active after error. query_resp=%s", response);
            return ESP_OK;
        }
        return err;
    }

    response[0] = '\0';
    err = sim7080g_hal_send_cmd(AT_CNACT_QUERY,
                                NULL,
                                response,
                                sizeof(response),
                                METER_SIM7080G_AT_TIMEOUT_MS,
                                METER_SIM7080G_MAX_RETRIES);
    if ((err == ESP_OK) && sim7080g_response_pdp_ready_for_cid(response, cid)) {
        return ESP_OK;
    }

    LOG_WARN(TAG, "CNACT query cid=%u not ready. resp=%s", (unsigned)cid, response);
    return ESP_FAIL;
}

static bool sim7080g_baud_already_tested(uint32_t baud, size_t tested_count, const uint32_t *tested)
{
    for (size_t i = 0U; i < tested_count; ++i) {
        if (tested[i] == baud) {
            return true;
        }
    }
    return false;
}

static esp_err_t sim7080g_probe_modem(void)
{
    uint32_t tested[sizeof(k_sim7080g_baud_candidates) / sizeof(k_sim7080g_baud_candidates[0])];
    size_t tested_count = 0U;

    for (size_t i = 0U; i < (sizeof(k_sim7080g_baud_candidates) / sizeof(k_sim7080g_baud_candidates[0])); ++i) {
        const uint32_t baud = k_sim7080g_baud_candidates[i];
        if (sim7080g_baud_already_tested(baud, tested_count, tested)) {
            continue;
        }
        tested[tested_count++] = baud;

        /* HAL init handles re-init safely: same config => flush+reuse,
           different baud => reconfigure without UART driver teardown. */
        const sim7080g_hal_config_t cfg = {
            .uart_num = SIM7080G_UART_NUM,
            .tx_pin = SIM7080G_UART_TX,
            .rx_pin = SIM7080G_UART_RX,
            .baudrate = baud
        };

        esp_err_t err = sim7080g_hal_init(&cfg);
        if (err != ESP_OK) {
            LOG_WARN(TAG, "HAL init failed @%lu", (unsigned long)baud);
            continue;
        }

        err = sim7080g_hal_send_cmd(AT_BASIC,
                                    NULL,
                                    NULL,
                                    0U,
                                    k_probe_at_timeout_ms,
                                    2U);
        if (err == ESP_OK) {
            s_detected_baudrate = baud;
            LOG_INFO(TAG,
                     "SIM7080G responded to AT @%lu (uart=%d tx=%d rx=%d)",
                     (unsigned long)s_detected_baudrate,
                     SIM7080G_UART_NUM,
                     SIM7080G_UART_TX,
                     SIM7080G_UART_RX);
            return ESP_OK;
        }

        LOG_WARN(TAG, "No AT response @%lu", (unsigned long)baud);
    }

    s_detected_baudrate = 0U;
    return ESP_ERR_TIMEOUT;
}

/**
 * @brief Inicializa modem SIM7080G y valida enlace AT base.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t sim7080g_init(void)
{
    if (sim7080g_is_modem_ready()) {
        // Health check ligero para detectar power-cycle del modem.
        esp_err_t err = sim7080g_hal_send_cmd(AT_BASIC,
                                              NULL,
                                              NULL,
                                              0U,
                                              k_probe_at_timeout_ms,
                                              1U);
        if (err == ESP_OK) {
            s_health_fail_streak = 0U;
            // Reaplica ATE0 por si el modem reinicio y volvio con echo activo.
            (void)sim7080g_hal_send_cmd(AT_ECHO_OFF,
                                        NULL,
                                        NULL,
                                        0U,
                                        METER_SIM7080G_AT_TIMEOUT_MS,
                                        1U);
            return ESP_OK;
        }

        s_health_fail_streak++;
        if (s_health_fail_streak < k_health_fail_force_reprobe) {
            LOG_WARN(TAG,
                     "modem health-check failed (%u/%u) - will retry before re-probe",
                     (unsigned)s_health_fail_streak,
                     (unsigned)k_health_fail_force_reprobe);
            return err;
        }

        LOG_WARN(TAG, "modem health-check failed repeatedly, forcing re-probe");
        /* Do NOT call sim7080g_hal_deinit() here: tearing down and
           reinstalling the UART driver triggers an ESP-IDF spinlock
           assert.  Instead, just reset state and let probe_modem()
           call hal_init() which will safely flush+reuse the existing
           driver. */
        s_state = 0U;
        s_health_fail_streak = 0U;
    }

    esp_err_t err = sim7080g_probe_modem();
    if (err != ESP_OK) {
        LOG_ERROR(TAG,
                  "SIM7080G no responde en UART%d tx=%d rx=%d. Revisar cableado, GND comun, alimentacion del modulo y baudrate.",
                  SIM7080G_UART_NUM,
                  SIM7080G_UART_TX,
                  SIM7080G_UART_RX);
        s_state = SIM7080G_STATE_ERROR;
        return err;
    }
    s_health_fail_streak = 0U;

    err = sim7080g_hal_send_cmd(AT_ECHO_OFF,
                                NULL,
                                NULL,
                                0U,
                                METER_SIM7080G_AT_TIMEOUT_MS,
                                METER_SIM7080G_MAX_RETRIES);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "ATE0 failed");
        s_state = SIM7080G_STATE_ERROR;
        return err;
    }

    err = sim7080g_hal_send_cmd(AT_CMEE_ON,
                                NULL,
                                NULL,
                                0U,
                                METER_SIM7080G_AT_TIMEOUT_MS,
                                METER_SIM7080G_MAX_RETRIES);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "CMEE enable failed (continuing)");
    }

    // Asegura modo UART sin RTS/CTS (el hardware actual usa solo TX/RX).
    err = sim7080g_hal_send_cmd(AT_IFC_NONE,
                                NULL,
                                NULL,
                                0U,
                                METER_SIM7080G_AT_TIMEOUT_MS,
                                1U);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "IFC disable failed (continuing)");
    }

    err = sim7080g_wait_sim_ready(k_sim_ready_timeout_ms);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "SIM not ready (CPIN)");
        s_state = SIM7080G_STATE_ERROR;
        return err;
    }

    err = sim7080g_hal_send_cmd(AT_CSQ_QUERY,
                                NULL,
                                NULL,
                                0U,
                                METER_SIM7080G_AT_TIMEOUT_MS,
                                METER_SIM7080G_MAX_RETRIES);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "CSQ query failed");
        s_state = SIM7080G_STATE_ERROR;
        return err;
    }

    {
        char cmd[32];
        (void)snprintf(cmd, sizeof(cmd), AT_CNMP_FMT, (unsigned)METER_SIM7080G_CNMP_MODE);
        err = sim7080g_hal_send_cmd(cmd,
                                    NULL,
                                    NULL,
                                    0U,
                                    METER_SIM7080G_AT_TIMEOUT_MS,
                                    METER_SIM7080G_MAX_RETRIES);
        if (err != ESP_OK) {
            LOG_WARN(TAG, "CNMP=%u failed (continuing)", (unsigned)METER_SIM7080G_CNMP_MODE);
        }
    }

    {
        char cmd[32];
        (void)snprintf(cmd, sizeof(cmd), AT_CMNB_FMT, (unsigned)METER_SIM7080G_CMNB_MODE);
        err = sim7080g_hal_send_cmd(cmd,
                                    NULL,
                                    NULL,
                                    0U,
                                    METER_SIM7080G_AT_TIMEOUT_MS,
                                    METER_SIM7080G_MAX_RETRIES);
        if (err != ESP_OK) {
            LOG_WARN(TAG, "CMNB=%u failed (continuing)", (unsigned)METER_SIM7080G_CMNB_MODE);
        }
    }

    // Mitigacion de inestabilidad por ahorro de energia del modem.
    err = sim7080g_hal_send_cmd(AT_CSCLK_OFF,
                                NULL,
                                NULL,
                                0U,
                                METER_SIM7080G_AT_TIMEOUT_MS,
                                1U);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "CSCLK disable failed (continuing)");
    }
    err = sim7080g_hal_send_cmd(AT_CPSMS_OFF,
                                NULL,
                                NULL,
                                0U,
                                METER_SIM7080G_AT_TIMEOUT_MS,
                                1U);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "CPSMS disable failed (continuing)");
    }
    err = sim7080g_hal_send_cmd(AT_CEDRXS_OFF,
                                NULL,
                                NULL,
                                0U,
                                METER_SIM7080G_AT_TIMEOUT_MS,
                                1U);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "CEDRXS disable failed (continuing)");
    }

    s_state = SIM7080G_STATE_MODEM_READY;
    LOG_INFO(TAG,
             "SIM7080G modem ready (baud=%lu)",
             (unsigned long)s_detected_baudrate);
    return ESP_OK;
}

esp_err_t sim7080g_wait_network(uint32_t timeout_ms)
{
    esp_err_t err = sim7080g_init();
    if (err != ESP_OK) {
        return err;
    }

    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    char response[256];

    while ((xTaskGetTickCount() - start) < timeout_ticks) {
        const char *reg_source = NULL;
        response[0] = '\0';
        err = sim7080g_query_network_registration(response,
                                                  sizeof(response),
                                                  &reg_source);
        if (err == ESP_OK) {
            s_state = (uint8_t)(s_state | SIM7080G_STATE_NETWORK_READY);
            s_state = (uint8_t)(s_state & ((uint8_t)~SIM7080G_STATE_ERROR));
            LOG_INFO(TAG,
                     "registro celular OK via %s",
                     (reg_source != NULL) ? reg_source : "AT");
            sim7080g_log_radio_info();
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(2000U));
    }

    sim7080g_log_radio_info();
    LOG_WARN(TAG, "network wait timeout. last_resp=%s", response);
    s_state = (uint8_t)(s_state & ((uint8_t)~SIM7080G_STATE_NETWORK_READY));
    s_state = (uint8_t)(s_state | SIM7080G_STATE_ERROR);
    return ESP_ERR_TIMEOUT;
}

esp_err_t sim7080g_activate_pdp(void)
{
    esp_err_t err = sim7080g_init();
    if (err != ESP_OK) {
        return err;
    }

    err = sim7080g_ensure_ps_attach();
    if (err != ESP_OK) {
        LOG_WARN(TAG, "PS attach failed before PDP activation");
        s_state = (uint8_t)(s_state | SIM7080G_STATE_ERROR);
        return err;
    }

    err = sim7080g_refresh_runtime_apn();
    if (err != ESP_OK) {
        LOG_WARN(TAG, "CGNAPN failed, usando APN configurado: %s", METER_SIM7080G_APN);
    }

    err = sim7080g_activate_pdp_cid((uint8_t)METER_SIM7080G_PDP_CID);
    if (err != ESP_OK) {
        sim7080g_log_ceer();
        s_state = (uint8_t)(s_state | SIM7080G_STATE_ERROR);
        return err;
    }

    s_state = (uint8_t)(s_state | SIM7080G_STATE_PDP_READY);
    s_state = (uint8_t)(s_state & ((uint8_t)~SIM7080G_STATE_ERROR));
    return ESP_OK;
}

esp_err_t sim7080g_cycle_pdp(void)
{
    char cmd[64];
    char response[128];

    // Desactiva PDP para forzar reset del stack TCP del modem.
    // Necesario cuando AT+SMDISC falla con "operation not allowed":
    // el SIM7080G tiene la FSM MQTT atascada en estado conectando y
    // solo un ciclo PDP libera la sesion TCP subyacente.
    (void)snprintf(cmd, sizeof(cmd), AT_CNACT_OFF_FMT, (unsigned)METER_SIM7080G_PDP_CID);
    response[0] = '\0';
    (void)sim7080g_hal_send_cmd(cmd, NULL, response, sizeof(response), 3000U, 1U);
    LOG_INFO(TAG, "PDP cycle deactivate. resp=%s", response);

    vTaskDelay(pdMS_TO_TICKS(2000U));

    s_state = (uint8_t)(s_state & ((uint8_t)~SIM7080G_STATE_PDP_READY));
    const esp_err_t err = sim7080g_activate_pdp_cid((uint8_t)METER_SIM7080G_PDP_CID);
    if (err == ESP_OK) {
        s_state = (uint8_t)(s_state | SIM7080G_STATE_PDP_READY);
        LOG_INFO(TAG, "PDP cycle reactivate OK");
    } else {
        LOG_WARN(TAG, "PDP cycle reactivate failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t sim7080g_recover_network(void)
{
    s_state = (uint8_t)(s_state &
                        ((uint8_t)~(SIM7080G_STATE_NETWORK_READY | SIM7080G_STATE_PDP_READY)));

    esp_err_t err = sim7080g_wait_network(METER_SIM7080G_NETWORK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    return sim7080g_activate_pdp();
}

esp_err_t sim7080g_refresh_runtime_state(void)
{
    esp_err_t err = sim7080g_init();
    if (err != ESP_OK) {
        return err;
    }

    char response[256];
    response[0] = '\0';
    err = sim7080g_query_network_registration(response,
                                              sizeof(response),
                                              NULL);
    if (err != ESP_OK) {
        s_state = (uint8_t)(s_state & ((uint8_t)~SIM7080G_STATE_NETWORK_READY));
        s_state = (uint8_t)(s_state & ((uint8_t)~SIM7080G_STATE_PDP_READY));
        return err;
    }

    response[0] = '\0';
    err = sim7080g_hal_send_cmd(AT_CGATT_QUERY,
                                NULL,
                                response,
                                sizeof(response),
                                k_runtime_query_timeout_ms,
                                1U);
    if ((err != ESP_OK) || !sim7080g_response_attached(response)) {
        s_state = (uint8_t)(s_state & ((uint8_t)~SIM7080G_STATE_PDP_READY));
        return (err == ESP_OK) ? ESP_ERR_INVALID_STATE : err;
    }

    response[0] = '\0';
    err = sim7080g_hal_send_cmd(AT_CNACT_QUERY,
                                NULL,
                                response,
                                sizeof(response),
                                k_runtime_query_timeout_ms,
                                1U);
    if ((err != ESP_OK) ||
        !sim7080g_response_pdp_ready_for_cid(response, (uint8_t)METER_SIM7080G_PDP_CID)) {
        LOG_WARN(TAG,
                 "runtime PDP invalid for cid=%u. resp=%s",
                 (unsigned)METER_SIM7080G_PDP_CID,
                 response);
        s_state = (uint8_t)(s_state & ((uint8_t)~SIM7080G_STATE_PDP_READY));
        return (err == ESP_OK) ? ESP_ERR_INVALID_STATE : err;
    }

    s_state = (uint8_t)(s_state | SIM7080G_STATE_NETWORK_READY | SIM7080G_STATE_PDP_READY);
    s_state = (uint8_t)(s_state & ((uint8_t)~SIM7080G_STATE_ERROR));
    return ESP_OK;
}

uint8_t sim7080g_get_state(void)
{
    return s_state;
}

bool sim7080g_is_modem_ready(void)
{
    return (s_state & SIM7080G_STATE_MODEM_READY) != 0U;
}

bool sim7080g_is_network_ready(void)
{
    return (s_state & SIM7080G_STATE_NETWORK_READY) != 0U;
}

bool sim7080g_is_pdp_ready(void)
{
    return (s_state & SIM7080G_STATE_PDP_READY) != 0U;
}

void sim7080g_mark_modem_unready(void)
{
    /* Flush UART RX buffer instead of full deinit to avoid spinlock
       corruption on subsequent re-init. */
    if (sim7080g_hal_is_ready()) {
        (void)sim7080g_hal_send_cmd("AT", NULL, NULL, 0U, 200U, 1U);
    }
    s_state = 0U;
    s_detected_baudrate = 0U;
    s_health_fail_streak = 0U;
    s_runtime_apn[0] = '\0';
}

const char *sim7080g_get_apn(void)
{
    return sim7080g_select_apn();
}

static char s_network_time[20];

esp_err_t sim7080g_sync_network_time(void)
{
    if (!sim7080g_hal_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    char resp[80] = {0};
    esp_err_t err = sim7080g_hal_send_cmd("AT+CCLK?", "+CCLK:", resp, sizeof(resp),
                                          METER_SIM7080G_AT_TIMEOUT_MS, 1U);
    if (err != ESP_OK) {
        return err;
    }

    // Response format: +CCLK: "YY/MM/DD,HH:MM:SS+ZZ"
    const char *q1 = strchr(resp, '"');
    if (q1 == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    ++q1;
    // Parse YY/MM/DD,HH:MM — acota los campos a uint8_t para que el compilador
    // pueda calcular el tamano maximo de snprintf (2 digitos por campo = 17 bytes).
    unsigned yy = 0U, mo = 0U, dd = 0U, hh = 0U, mi = 0U;
    if (sscanf(q1, "%u/%u/%u,%u:%u", &yy, &mo, &dd, &hh, &mi) < 5) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    // Reject default/invalid time (year 0 or 80 means not synced)
    if ((yy == 0U) || (yy == 80U)) {
        return ESP_ERR_NOT_FOUND;
    }
    const uint8_t yy8 = (uint8_t)(yy % 100U);
    const uint8_t mo8 = (uint8_t)(mo % 100U);
    const uint8_t dd8 = (uint8_t)(dd % 100U);
    const uint8_t hh8 = (uint8_t)(hh % 100U);
    const uint8_t mi8 = (uint8_t)(mi % 100U);
    (void)snprintf(s_network_time, sizeof(s_network_time),
                   "20%02u-%02u-%02u %02u:%02u",
                   (unsigned)yy8, (unsigned)mo8, (unsigned)dd8,
                   (unsigned)hh8, (unsigned)mi8);
    return ESP_OK;
}

const char *sim7080g_get_network_time(void)
{
    return s_network_time;
}





