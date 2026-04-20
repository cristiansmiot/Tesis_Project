#ifndef TASK_COMMUNICATION_H
#define TASK_COMMUNICATION_H

/**
 * @file task_communication.h
 * @brief Task Communication - Sprint 3
 * Responsabilidad: Communication task launcher API.
 * Interfaz de hardware: SIM7080G
 * Protocolo: FreeRTOS task
 * Integracion prevista:
 *   - task_manager may call task_communication_start.
 *   - Flujo de datos: meter_data_get_snapshot() -> MeterData_t
 * Pines reservados en board_config.h: SIM7080G UART1 TX=16 RX=17
 */

#include "esp_err.h"

/**
 * @brief Crea y arranca la tarea de comunicacion.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t task_communication_start(void);

#endif // TASK_COMMUNICATION_H

