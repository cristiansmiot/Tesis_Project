#ifndef MSURE_H
#define MSURE_H

/**
 * @file msure.h
 * @brief mSure Calibration - Sprint 1.2
 * Responsabilidad: mSure autocalibration orchestration.
 * Interfaz de hardware: ADE9153A via SPI
 * Protocolo: ADE9153A register access
 * Integracion prevista:
 *   - task_calibration will call msure_init.
 *   - Flujo de datos: meter_data_get_snapshot() -> MeterData_t
 * Pines reservados en board_config.h: Uses ADE9153A SPI pins
 */

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MSURE_MODE_AI_NORMAL = 0,
    MSURE_MODE_AI_TURBO  = 1
} msure_mode_t;

typedef struct {
    int32_t aicc_raw;
    int32_t aicert_raw;
    int32_t avcc_raw;
    int32_t avcert_raw;
    float aicc_nA_per_code;
    float avcc_nV_per_code;
    int32_t aigain;
    int32_t avgain;
    msure_mode_t mode;
} msure_result_t;

/**
 * @brief Inicializa el modulo mSure.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t msure_init(void);

/**
 * @brief Ejecuta mSure en canal A (normal/turbo) y/o canal V.
 * @param run_channel_a true para ejecutar autocalibracion de corriente A.
 * @param run_channel_v true para ejecutar autocalibracion de voltaje.
 * @param mode Modo para autocalibracion de corriente A.
 * @param out Resultado de autocalibracion y ganancias aplicadas.
 * @return ESP_OK si el flujo completo finaliza correctamente.
 */
esp_err_t msure_run(bool run_channel_a, bool run_channel_v, msure_mode_t mode, msure_result_t *out);

/**
 * @brief Evalua si las certezas mSure cumplen umbral de aceptacion.
 * @param result Resultado de mSure.
 * @return true si AICERT y AVCERT cumplen umbral.
 */
bool msure_cert_is_valid(const msure_result_t *result);

/**
 * @brief Evalua si el resultado mSure luce fisicamente plausible.
 * @param result Resultado de mSure.
 * @return true si las constantes medidas estan en rango esperable.
 */
bool msure_result_is_plausible(const msure_result_t *result);

#endif // MSURE_H

