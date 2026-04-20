#ifndef REACTIVO_POWER_H
#define REACTIVO_POWER_H

#include "esp_err.h"

/**
 * @brief Inicializa el modulo de potencia reactiva.
 * @param target_varcc Constante [uVAR/code].
 * @param target_varhcc Constante [nVARh/code].
 * @return ESP_OK en caso de exito.
 */
esp_err_t reactive_power_init(float target_varcc, float target_varhcc);

/**
 * @brief Obtiene potencia reactiva instantanea.
 * @param void Sin parametros.
 * @return Potencia reactiva en VAR.
 */
float reactive_power_get_var(void);

/**
 * @brief Obtiene energia reactiva acumulada.
 * @param void Sin parametros.
 * @return Energia reactiva en VARh.
 */
float reactive_power_get_varh(void);

#endif // REACTIVO_POWER_H

