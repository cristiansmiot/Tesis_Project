#ifndef TASK_POWER_MGMT_H
#define TASK_POWER_MGMT_H

/**
 * @file task_power_mgmt.h
 * @brief Task Power Mgmt - Sprint 5
 * Responsabilidad: Power management task launcher API.
 * Interfaz de hardware: Power control block
 * Protocolo: FreeRTOS task
 * Integracion prevista:
 *   - task_manager may call task_power_mgmt_start.
 *   - Flujo de datos: meter_data_get_snapshot() -> MeterData_t
 * Pines reservados en board_config.h: BATT_ADC=4 POWER_DETECT=5 BACKUP_ENABLE=6
 */

#include "esp_err.h"

/**
 * @brief Inicializacion stub para etapa de sprint.
 * @param void Sin parametros.
 * @return ESP_ERR_NOT_SUPPORTED hasta implementacion del sprint.
 */
esp_err_t task_power_mgmt_start(void);

#endif // TASK_POWER_MGMT_H

