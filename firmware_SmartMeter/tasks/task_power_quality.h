#ifndef TASK_POWER_QUALITY_H
#define TASK_POWER_QUALITY_H

/**
 * @file task_power_quality.h
 * @brief Task Power Quality - Sprint 1.2
 * Responsabilidad: Power quality task launcher API.
 * Interfaz de hardware: ADE9153A
 * Protocolo: FreeRTOS task
 * Integracion prevista:
 *   - task_manager may call task_power_quality_start.
 *   - Flujo de datos: meter_data_get_snapshot() -> MeterData_t
 * Pines reservados en board_config.h: Uses ADE9153A SPI pins
 */

#include "esp_err.h"

/**
 * @brief Crea y arranca task_power_quality.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t task_power_quality_start(void);

#endif // TASK_POWER_QUALITY_H

