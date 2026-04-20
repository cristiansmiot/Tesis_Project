#ifndef DATA_BUFFER_H
#define DATA_BUFFER_H

/**
 * @file data_buffer.h
 * @brief Data Buffer - Sprint 1.2
 * Responsabilidad: Buffered history storage API.
 * Interfaz de hardware: Sin hardware directo
 * Protocolo: In-memory ring buffer
 * Integracion prevista:
 *   - communication modules may call data_buffer_init.
 *   - Flujo de datos: meter_data_get_snapshot() -> MeterData_t
 * Pines reservados en board_config.h: Sin pin dedicado
 */

#include "esp_err.h"

/**
 * @brief Inicializacion stub para etapa de sprint.
 * @param void Sin parametros.
 * @return ESP_ERR_NOT_SUPPORTED hasta implementacion del sprint.
 */
esp_err_t data_buffer_init(void);

#endif // DATA_BUFFER_H

