#include "node_health.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "meter_config.h"
#include "logger.h"
#include "sim7080g_hal.h"
#include "sim7080g_init.h"

static const char *TAG = "node_health";
static const uint32_t k_rssi_timeout_ms = 1000U;

static uint32_t s_msg_tx = 0U;
static uint32_t s_data_generated;
static uint32_t s_data_publish_attempts;
static bool s_initialized;
static uint32_t s_mqtt_successes = 0U;
static uint32_t s_mqtt_attempts = 0U;
static uint32_t s_cellular_attempts = 0U;
static uint32_t s_cellular_successes = 0U;
static uint32_t s_ade_losses = 0U;
static uint32_t s_ade_recovery_successes = 0U;
static uint32_t s_ac_losses = 0U;
static uint32_t s_ac_restores = 0U;

esp_err_t node_health_init(void)
{
    if (s_initialized) { return ESP_OK; }
    s_initialized = true;
    s_msg_tx = 0U;
    s_mqtt_successes = 0U;
    s_mqtt_attempts = 0U;
    s_cellular_attempts = 0U;
    s_cellular_successes = 0U;
    s_ade_losses = 0U;
    s_ade_recovery_successes = 0U;
    s_ac_losses = 0U;
    s_ac_restores = 0U;
    return ESP_OK;
}

void node_health_msg_tx_inc(void)
{
    s_msg_tx++;
}

uint32_t node_health_data_generated_inc(void)
{
    return __atomic_add_fetch(&s_data_generated, 1U, __ATOMIC_RELAXED);
}

void node_health_data_publish_attempt_inc(void)
{
    ++s_data_publish_attempts;
}

uint32_t node_health_get_data_generated(void)
{
    return __atomic_load_n(&s_data_generated, __ATOMIC_RELAXED);
}

uint32_t node_health_get_data_publish_attempts(void)
{
    return s_data_publish_attempts;
}

esp_err_t node_health_refresh_radio(sim7080g_radio_t *out)
{
    if (out == NULL) { return ESP_ERR_INVALID_ARG; }
    *out = (sim7080g_radio_t){0};
    if (!sim7080g_is_modem_ready() || !sim7080g_is_network_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    char response[384] = {0};
    const esp_err_t err = sim7080g_hal_send_cmd("AT+CPSI?", NULL,
        response, sizeof(response), 1500U, 1U);
    if (err != ESP_OK) { return err; }
    if (!sim7080g_radio_parse_cpsi(response, out)) {
        LOG_WARN(TAG, "CPSI unavailable/unsupported encoding: %s", response);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

void node_health_reconnect_inc(void)
{
    node_health_mqtt_success_inc();
}

void node_health_cellular_attempt_inc(void)
{
    s_cellular_attempts++;
}

void node_health_cellular_success_inc(void)
{
    s_cellular_successes++;
}

void node_health_mqtt_attempt_inc(void)
{
    s_mqtt_attempts++;
}

void node_health_mqtt_success_inc(void)
{
    s_mqtt_successes++;
}

void node_health_ade_loss_inc(void)
{
    s_ade_losses++;
}

void node_health_ade_recovery_success_inc(void)
{
    s_ade_recovery_successes++;
}

void node_health_ac_loss_inc(void)
{
    s_ac_losses++;
}

void node_health_ac_restore_inc(void)
{
    s_ac_restores++;
}

esp_err_t node_health_get_rssi_dbm(int8_t *rssi_dbm)
{
    if (rssi_dbm == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!sim7080g_is_modem_ready() || !sim7080g_is_network_ready()) {
        *rssi_dbm = (int8_t)INT8_MIN;
        return ESP_FAIL;
    }

    char response[64];
    response[0] = '\0';

    const esp_err_t err = sim7080g_hal_send_cmd("AT+CSQ",
                                                NULL,
                                                response,
                                                sizeof(response),
                                                k_rssi_timeout_ms,
                                                1U);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "AT+CSQ failed: %s", esp_err_to_name(err));
        *rssi_dbm = (int8_t)INT8_MIN;
        return ESP_FAIL;
    }

    const char *prefix = strstr(response, "+CSQ:");
    if (prefix == NULL) {
        LOG_WARN(TAG, "AT+CSQ unexpected response: %s", response);
        *rssi_dbm = (int8_t)INT8_MIN;
        return ESP_FAIL;
    }

    prefix += strlen("+CSQ:");
    while (*prefix == ' ') {
        ++prefix;
    }

    const int rssi = atoi(prefix);
    if (rssi == 99) {
        *rssi_dbm = (int8_t)INT8_MIN;
        return ESP_FAIL;
    }

    if (rssi == 31) {
        *rssi_dbm = -51;
    } else if ((rssi >= 0) && (rssi <= 30)) {
        *rssi_dbm = (int8_t)(-113 + (rssi * 2));
    } else {
        *rssi_dbm = (int8_t)INT8_MIN;
        return ESP_FAIL;
    }

    return ESP_OK;
}

uint32_t node_health_get_msg_tx(void)
{
    return s_msg_tx;
}

uint32_t node_health_get_reconnects(void)
{
    return s_mqtt_successes > 0U ? s_mqtt_successes - 1U : 0U;
}

uint32_t node_health_get_cellular_attempts(void)
{
    return s_cellular_attempts;
}

uint32_t node_health_get_cellular_successes(void)
{
    return s_cellular_successes;
}

uint32_t node_health_get_mqtt_attempts(void)
{
    return s_mqtt_attempts;
}

uint32_t node_health_get_mqtt_successes(void)
{
    return s_mqtt_successes;
}

uint32_t node_health_get_ade_losses(void)
{
    return s_ade_losses;
}

uint32_t node_health_get_ade_recovery_successes(void)
{
    return s_ade_recovery_successes;
}

uint32_t node_health_get_ac_losses(void)
{
    return s_ac_losses;
}

uint32_t node_health_get_ac_restores(void)
{
    return s_ac_restores;
}
