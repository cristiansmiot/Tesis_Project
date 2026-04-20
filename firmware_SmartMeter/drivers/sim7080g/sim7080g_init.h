#ifndef SIM7080G_INIT_H
#define SIM7080G_INIT_H

/**
 * @file sim7080g_init.h
 * @brief SIM7080G Init - Sprint 3
 * Responsabilidad: SIM7080G modem bring-up entry point.
 * Interfaz de hardware: UART
 * Protocolo: AT commands
 * Integracion prevista:
 *   - task_communication will call sim7080g_init.
 *   - Flujo de datos: meter_data_get_snapshot() -> MeterData_t
 * Pines reservados en board_config.h: SIM7080G UART1 TX=16 RX=17
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define SIM7080G_STATE_MODEM_READY   (1U << 0)
#define SIM7080G_STATE_NETWORK_READY (1U << 1)
#define SIM7080G_STATE_PDP_READY     (1U << 2)
#define SIM7080G_STATE_ERROR         (1U << 7)

/**
 * @brief Inicializa modem SIM7080G y valida enlace AT.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t sim7080g_init(void);

/**
 * @brief Espera registro de red (CEREG/CGREG stat=1/5).
 * @param timeout_ms Timeout total.
 * @return ESP_OK si registro exitoso.
 */
esp_err_t sim7080g_wait_network(uint32_t timeout_ms);

/**
 * @brief Activa PDP con APN configurado y valida estado.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t sim7080g_activate_pdp(void);

/**
 * @brief Flujo compacto de recuperacion de conectividad (red + PDP).
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t sim7080g_recover_network(void);

/**
 * @brief Cicla el contexto PDP (desactiva → 2s → reactiva).
 * Necesario cuando AT+SMDISC falla con "operation not allowed":
 * libera la sesion TCP subyacente para que MQTT pueda reconectar.
 * @return ESP_OK si el contexto se reactivó correctamente.
 */
esp_err_t sim7080g_cycle_pdp(void);

/**
 * @brief Verifica estado runtime de red+attach+PDP consultando AT.
 * @param void Sin parametros.
 * @return ESP_OK si red y PDP siguen listos.
 */
esp_err_t sim7080g_refresh_runtime_state(void);

/**
 * @brief Obtiene mascara de estado interna.
 * @param void Sin parametros.
 * @return Bits SIM7080G_STATE_*.
 */
uint8_t sim7080g_get_state(void);

/**
 * @brief Verifica disponibilidad del modem.
 * @param void Sin parametros.
 * @return true si modem esta listo.
 */
bool sim7080g_is_modem_ready(void);

/**
 * @brief Verifica si la red celular esta lista.
 * @param void Sin parametros.
 * @return true si hay registro de red.
 */
bool sim7080g_is_network_ready(void);

/**
 * @brief Verifica si el contexto PDP esta activo.
 * @param void Sin parametros.
 * @return true si PDP esta activo.
 */
bool sim7080g_is_pdp_ready(void);

/**
 * @brief Fuerza reset de estado interno modem/UART para re-probe completo.
 * @param void Sin parametros.
 */
void sim7080g_mark_modem_unready(void);

/**
 * @brief Obtiene APN en uso (auto-detectado o configurado).
 * @param void Sin parametros.
 * @return APN actual.
 */
const char *sim7080g_get_apn(void);

/**
 * @brief Lee el IMEI del SIM7080G via AT+GSN y lo almacena internamente.
 * @return ESP_OK si se obtuvo un IMEI de 15 dígitos.
 */
esp_err_t sim7080g_read_imei(void);

/**
 * @brief Retorna el IMEI leído (cadena vacía si aún no se leyó).
 */
const char *sim7080g_get_imei(void);

/**
 * @brief Solicita fecha/hora de la red via AT+CCLK? y la almacena internamente.
 * @return ESP_OK si se obtuvo hora valida.
 */
esp_err_t sim7080g_sync_network_time(void);

/**
 * @brief Obtiene la ultima fecha/hora sincronizada desde la red.
 * @return Cadena "YYYY-MM-DD HH:MM" o cadena vacia si no disponible.
 */
const char *sim7080g_get_network_time(void);

#endif // SIM7080G_INIT_H


