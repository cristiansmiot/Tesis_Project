#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

/**
 * @file mqtt_client.h
 * @brief MQTT Client - Sprint 3
 * Responsabilidad: MQTT transport for telemetry upload.
 * Interfaz de hardware: SIM7080G modem
 * Protocolo: MQTT over NB-IoT/LTE-M
 * Integracion prevista:
 *   - task_communication will call mqtt_client_init.
 *   - Flujo de datos: meter_data_get_snapshot() -> MeterData_t
 * Pines reservados en board_config.h: SIM7080G UART1 TX=16 RX=17
 */

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Inicializa la capa MQTT del SIM7080G (parametros de broker).
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t mqtt_client_init(void);

/**
 * @brief Establece sesion MQTT con el broker.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t mqtt_client_connect(void);

/**
 * @brief Cierra sesion MQTT activa.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t mqtt_client_disconnect(void);

/**
 * @brief Publica payload en topico MQTT.
 * @param topic Topico destino.
 * @param payload Mensaje a publicar (JSON).
 * @param qos Nivel QoS (0/1).
 * @param retain Flag retain.
 * @return ESP_OK en caso de exito.
 */
esp_err_t mqtt_client_publish(const char *topic, const char *payload, int qos, int retain);

/**
 * @brief Retorna estado cacheado de sesion MQTT (no bloquea).
 * @param void Sin parametros.
 * @return true si hay sesion activa.
 */
bool mqtt_client_is_connected(void);

/**
 * @brief Consulta estado de sesion MQTT via AT (puede bloquear).
 * @param void Sin parametros.
 * @return true si hay sesion activa.
 */
bool mqtt_client_check_connected(void);

/**
 * @brief Recupera conectividad MQTT (red/PDP + reconexion MQTT).
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t mqtt_client_recover(void);

/**
 * @brief Suscribe a topico de comandos para recibir acciones del backend.
 * @param topic Topico a suscribir (ej. "medidor/ESP32-001/cmd").
 * @return ESP_OK en caso de exito.
 */
esp_err_t mqtt_client_subscribe(const char *topic);

/**
 * @brief Lee comando pendiente del broker (URC +SMSUB).
 * @param cmd_out Buffer para almacenar el comando recibido.
 * @param cmd_len Tamanio del buffer.
 * @return ESP_OK si se recibio comando, ESP_ERR_NOT_FOUND si no hay.
 */
esp_err_t mqtt_client_read_command(char *cmd_out, size_t cmd_len);

#endif // MQTT_CLIENT_H

