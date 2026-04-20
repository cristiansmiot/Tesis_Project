#ifndef PQ_MONITOR_H
#define PQ_MONITOR_H

/**
 * @file pq_monitor.h
 * @brief PQ Monitor - Sprint 1.2
 * Responsabilidad: Power quality monitor and alert flags.
 * Interfaz de hardware: ADE9153A via SPI
 * Protocolo: ADE9153A register access
 * Integracion prevista:
 *   - task_power_quality will call pq_monitor_init.
 *   - Flujo de datos: meter_data_get_snapshot() -> MeterData_t
 * Pines reservados en board_config.h: Uses ADE9153A SPI pins
 */

#include "esp_err.h"
#include <stdint.h>
#include "meter_data.h"

#define PQ_FLAG_SAG       (1U << 0)
#define PQ_FLAG_SWELL     (1U << 1)
#define PQ_FLAG_FREQ_OOR  (1U << 2)
#define PQ_FLAG_OVERTEMP  (1U << 3)

/**
 * @brief Inicializa monitor de calidad de potencia.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t pq_monitor_init(void);

/**
 * @brief Procesa un snapshot metrologico y actualiza flags de PQ.
 * @param snap Snapshot de medicion.
 * @return Mascara PQ_FLAG_* consolidada.
 */
uint32_t pq_monitor_process_snapshot(const MeterData_t *snap);

/**
 * @brief Obtiene flags de PQ vigentes.
 * @param void Sin parametros.
 * @return Mascara PQ_FLAG_*.
 */
uint32_t pq_monitor_get_flags(void);

#endif // PQ_MONITOR_H

