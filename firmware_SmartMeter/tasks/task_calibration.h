#ifndef TASK_CALIBRATION_H
#define TASK_CALIBRATION_H

/**
 * @file task_calibration.h
 * @brief Task Calibration - Sprint 1.2
 * Responsabilidad: Calibration task launcher API.
 * Interfaz de hardware: ADE9153A + NVS
 * Protocolo: FreeRTOS task
 * Integracion prevista:
 *   - task_manager may call task_calibration_start.
 *   - Flujo de datos: meter_data_get_snapshot() -> MeterData_t
 * Pines reservados en board_config.h: Uses ADE9153A SPI pins
 */

#include "esp_err.h"
#include <stdint.h>

#define CALIB_STATUS_RUNNING   (1U << 0)
#define CALIB_STATUS_VALID     (1U << 1)
#define CALIB_STATUS_PERSISTED (1U << 2)
#define CALIB_STATUS_LOADED    (1U << 3)
#define CALIB_STATUS_ERROR     (1U << 7)

/**
 * @brief Crea y arranca task_calibration.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t task_calibration_start(void);

/**
 * @brief Obtiene estado actual de calibracion.
 * @param void Sin parametros.
 * @return Mascara CALIB_STATUS_*.
 */
uint8_t task_calibration_get_status(void);

#endif // TASK_CALIBRATION_H

