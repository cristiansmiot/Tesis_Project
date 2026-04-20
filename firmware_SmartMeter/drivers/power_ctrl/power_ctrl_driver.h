#ifndef POWER_CTRL_DRIVER_H
#define POWER_CTRL_DRIVER_H

/**
 * @file power_ctrl_driver.h
 * @brief Power Control Driver - Sprint 5
 * Responsabilidad: AC/DC and battery control driver.
 * Interfaz de hardware: GPIO + ADC
 * Protocolo: Direct GPIO/ADC
 * Integracion prevista:
 *   - task_power_mgmt will call power_ctrl_driver_init.
 *   - Flujo de datos: meter_data_get_snapshot() -> MeterData_t
 * Pines reservados en board_config.h: BATT_ADC=4 POWER_DETECT=5 BACKUP_ENABLE=6
 */

#include "esp_err.h"

/**
 * @brief Inicializacion stub para etapa de sprint.
 * @param void Sin parametros.
 * @return ESP_ERR_NOT_SUPPORTED hasta implementacion del sprint.
 */
esp_err_t power_ctrl_driver_init(void);

#endif // POWER_CTRL_DRIVER_H

