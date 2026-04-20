#ifndef SAG_SWELL_H
#define SAG_SWELL_H

/**
 * @file sag_swell.h
 * @brief Sag/Swell - Sprint 1.2
 * Responsabilidad: Voltage sag and swell detection.
 * Interfaz de hardware: ADE9153A via SPI
 * Protocolo: ADE9153A register access
 * Integracion prevista:
 *   - task_power_quality will call sag_swell_init.
 *   - Flujo de datos: meter_data_get_snapshot() -> MeterData_t
 * Pines reservados en board_config.h: Uses ADE9153A SPI pins
 */

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define SAG_SWELL_FLAG_SAG   (1U << 0)
#define SAG_SWELL_FLAG_SWELL (1U << 1)

/**
 * @brief Inicializa detector de sag/swell.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t sag_swell_init(void);

/**
 * @brief Evalua flags de sag/swell para la muestra actual.
 * @param vrms Voltaje RMS ya convertido a volts.
 * @param ac_present true cuando la linea AC esta presente.
 * @return Mascara SAG_SWELL_FLAG_*.
 */
uint32_t sag_swell_eval(float vrms, bool ac_present);

#endif // SAG_SWELL_H

