#ifndef NVM_CALIB_H
#define NVM_CALIB_H

/**
 * @file nvm_calib.h
 * @brief Calibration NVM - Sprint 1.2
 * Responsabilidad: Calibration constants persistence API.
 * Interfaz de hardware: Internal flash/NVS
 * Protocolo: ESP-IDF NVS
 * Integracion prevista:
 *   - task_calibration will call nvm_calib_init.
 *   - Flujo de datos: meter_data_get_snapshot() -> MeterData_t
 * Pines reservados en board_config.h: Sin pin dedicado
 */

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define NVM_CALIB_RECORD_MAGIC   0x4D43414CU  // "MCAL"
#define NVM_CALIB_RECORD_VERSION 1U

typedef struct {
    uint32_t magic;
    uint32_t version;
    int64_t timestamp_us;
    int32_t aicc_raw;
    int32_t aicert_raw;
    int32_t avcc_raw;
    int32_t avcert_raw;
    float aicc_nA_per_code;
    float avcc_nV_per_code;
    int32_t aigain;
    int32_t avgain;
    uint8_t mode;
    uint8_t reserved[3];
} nvm_calib_record_t;

/**
 * @brief Inicializa backend NVS para constantes de calibracion.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t nvm_calib_init(void);

/**
 * @brief Carga registro de calibracion desde NVS.
 * @param out Registro de salida.
 * @return ESP_OK si existe y es valido.
 */
esp_err_t nvm_calib_load(nvm_calib_record_t *out);

/**
 * @brief Persiste registro de calibracion en NVS.
 * @param record Registro a guardar.
 * @return ESP_OK si el commit finaliza correctamente.
 */
esp_err_t nvm_calib_save(const nvm_calib_record_t *record);

/**
 * @brief Aplica ganancias persistidas al ADE9153A.
 * @param record Registro con ganancias calculadas.
 * @return ESP_OK en caso de exito.
 */
esp_err_t nvm_calib_apply(const nvm_calib_record_t *record);

/**
 * @brief Indica si el backend NVS de calibracion esta listo.
 * @param void Sin parametros.
 * @return true si init/open fue exitoso.
 */
bool nvm_calib_is_ready(void);

#endif // NVM_CALIB_H

