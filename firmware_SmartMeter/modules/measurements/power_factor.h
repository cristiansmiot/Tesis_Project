#ifndef POWER_FACTOR_H
#define POWER_FACTOR_H

#include "esp_err.h"

/**
 * @brief Inicializa el modulo de factor de potencia.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t power_factor_init(void);

/**
 * @brief Obtiene factor de potencia.
 * @param void Sin parametros.
 * @return PF en rango aproximado [-1.0, +1.0].
 */
float power_factor_get(void);

#endif // POWER_FACTOR_H
