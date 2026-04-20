#ifndef OLED_HAL_H
#define OLED_HAL_H

/**
 * @file oled_hal.h
 * @brief OLED HAL - Sprint 4
 * Responsabilidad: Hardware abstraction for OLED display bus.
 * Interfaz de hardware: I2C
 * Protocolo: SSD1306 style I2C
 * Integracion prevista:
 *   - display_manager will use this HAL.
 *   - Flujo de datos: meter_data_get_snapshot() -> MeterData_t
 * Pines reservados en board_config.h: OLED SDA=8 SCL=9
 */

#include "esp_err.h"

/**
 * @brief Inicializacion stub para etapa de sprint.
 * @param void Sin parametros.
 * @return ESP_ERR_NOT_SUPPORTED hasta implementacion del sprint.
 */
esp_err_t oled_hal_init(void);

#endif // OLED_HAL_H

