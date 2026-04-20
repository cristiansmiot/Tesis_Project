#ifndef SIM7080G_HAL_H
#define SIM7080G_HAL_H

/**
 * @file sim7080g_hal.h
 * @brief SIM7080G HAL - Sprint 3
 * Responsabilidad: Hardware abstraction for SIM7080G modem.
 * Interfaz de hardware: UART
 * Protocolo: AT commands
 * Integracion prevista:
 *   - task_communication will use this HAL.
 *   - Flujo de datos: meter_data_get_snapshot() -> MeterData_t
 * Pines reservados en board_config.h: SIM7080G UART1 TX=16 RX=17
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    int uart_num;
    int tx_pin;
    int rx_pin;
    uint32_t baudrate;
} sim7080g_hal_config_t;

/**
 * @brief Inicializa UART y recursos de sincronizacion del HAL.
 * @param cfg Configuracion de UART del modem.
 * @return ESP_OK en caso de exito.
 */
esp_err_t sim7080g_hal_init(const sim7080g_hal_config_t *cfg);

/**
 * @brief Libera recursos de UART del HAL.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t sim7080g_hal_deinit(void);

/**
 * @brief Envia un AT y espera token esperado, OK o ERROR.
 * @param cmd Comando AT sin CRLF final.
 * @param expected Token esperado (opcional, puede ser NULL).
 * @param response Buffer de salida opcional para respuesta completa.
 * @param response_len Longitud del buffer de salida.
 * @param timeout_ms Timeout por intento.
 * @param retries Numero de reintentos.
 * @return ESP_OK si se obtuvo respuesta valida.
 */
esp_err_t sim7080g_hal_send_cmd(const char *cmd,
                                const char *expected,
                                char *response,
                                size_t response_len,
                                uint32_t timeout_ms,
                                uint8_t retries);

/**
 * @brief Envia payload al modem y espera fin con OK o token esperado.
 * @param payload Datos a enviar.
 * @param append_crlf true para anexar CRLF al final.
 * @param expected Token esperado (opcional, NULL => espera OK).
 * @param timeout_ms Timeout de espera.
 * @return ESP_OK en caso de exito.
 */
esp_err_t sim7080g_hal_send_payload(const char *payload,
                                    bool append_crlf,
                                    const char *expected,
                                    uint32_t timeout_ms);

/**
 * @brief Verifica si el HAL fue inicializado.
 * @param void Sin parametros.
 * @return true si esta listo.
 */
bool sim7080g_hal_is_ready(void);

/**
 * @brief Lee URCs pendientes del UART sin enviar comando.
 * Busca lineas que contengan el prefijo indicado (ej. "+SMSUB:").
 * @param prefix Prefijo URC a buscar.
 * @param out Buffer de salida para la linea completa encontrada.
 * @param out_len Tamanio del buffer de salida.
 * @param timeout_ms Tiempo maximo de espera.
 * @return ESP_OK si se encontro URC, ESP_ERR_NOT_FOUND si no.
 */
esp_err_t sim7080g_hal_read_urc(const char *prefix,
                                 char *out,
                                 size_t out_len,
                                 uint32_t timeout_ms);

#endif // SIM7080G_HAL_H

