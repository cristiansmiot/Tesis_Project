#ifndef ACTIVO_POWER_H
#define ACTIVO_POWER_H

#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Inicializa el modulo de potencia activa.
 * @param target_wcc Constante [uW/code].
 * @param target_whcc Constante [nWh/code].
 * @return ESP_OK en caso de exito.
 */
esp_err_t active_power_init(float target_wcc, float target_whcc);

/**
 * @brief Obtiene potencia activa instantanea.
 * @param void Sin parametros.
 * @return Potencia activa en watts.
 */
float active_power_get_watts(void);

/**
 * @brief Obtiene energia activa acumulada.
 * @param void Sin parametros.
 * @return Energia activa en Wh.
 */
float active_power_get_wh(void);

/**
 * @brief Obtiene el registro crudo AWATTHR_HI para diagnostico.
 * @param void Sin parametros.
 * @return Valor signed del registro AWATTHR_HI.
 */
int32_t active_power_get_wh_raw_hi(void);

#endif // ACTIVO_POWER_H

