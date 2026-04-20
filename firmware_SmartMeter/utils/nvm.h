#ifndef NVM_H
#define NVM_H

/**
 * @file nvm.h
 * @brief NVM Utility - Sprint 1.2
 * Responsabilidad: Generic non-volatile storage helpers.
 * Interfaz de hardware: Internal flash/NVS
 * Protocolo: ESP-IDF NVS
 * Integracion prevista:
 *   - calibration modules may call nvm_init.
 *   - Flujo de datos: meter_data_get_snapshot() -> MeterData_t
 * Pines reservados en board_config.h: Sin pin dedicado
 */

#include "esp_err.h"

/**
 * @brief Inicializacion stub para etapa de sprint.
 * @param void Sin parametros.
 * @return ESP_ERR_NOT_SUPPORTED hasta implementacion del sprint.
 */
esp_err_t nvm_init(void);

#endif // NVM_H

