#ifndef NODE_HEALTH_H
#define NODE_HEALTH_H

/**
 * @file node_health.h
 * @brief Node health counters and connectivity helpers.
 *
 * Tracks:
 * - Successful /datos publishes
 * - Cellular recovery attempts and successes
 * - MQTT connect attempts and successes
 * - ADE link losses and successful recoveries
 * - Cellular RSSI via AT+CSQ
 */

#include <stdint.h>
#include "esp_err.h"

esp_err_t node_health_init(void);

void node_health_msg_tx_inc(void);
void node_health_reconnect_inc(void);
void node_health_cellular_attempt_inc(void);
void node_health_cellular_success_inc(void);
void node_health_mqtt_attempt_inc(void);
void node_health_mqtt_success_inc(void);
void node_health_ade_loss_inc(void);
void node_health_ade_recovery_success_inc(void);

esp_err_t node_health_get_rssi_dbm(int8_t *rssi_dbm);

uint32_t node_health_get_msg_tx(void);
uint32_t node_health_get_reconnects(void);
uint32_t node_health_get_cellular_attempts(void);
uint32_t node_health_get_cellular_successes(void);
uint32_t node_health_get_mqtt_attempts(void);
uint32_t node_health_get_mqtt_successes(void);
uint32_t node_health_get_ade_losses(void);
uint32_t node_health_get_ade_recovery_successes(void);

#endif // NODE_HEALTH_H
