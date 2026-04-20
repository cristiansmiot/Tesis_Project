#ifndef HARMONICS_H
#define HARMONICS_H

/**
 * @file harmonics.h
 * @brief Harmonics - Sprint 2
 * Responsabilidad: Harmonic analysis module.
 * Interfaz de hardware: ADE9153A via SPI
 * Protocolo: ADE9153A register access
 * Integracion prevista:
 *   - task_power_quality will call harmonics_init.
 *   - Flujo de datos: meter_data_get_snapshot() -> MeterData_t
 * Pines reservados en board_config.h: Uses ADE9153A SPI pins
 */

#include "esp_err.h"

/**
 * @brief Inicializacion stub para etapa de sprint.
 * @param void Sin parametros.
 * @return ESP_ERR_NOT_SUPPORTED hasta implementacion del sprint.
 */
esp_err_t harmonics_init(void);

#endif // HARMONICS_H

