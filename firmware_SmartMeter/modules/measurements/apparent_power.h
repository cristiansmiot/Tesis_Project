#ifndef APPARENT_POWER_H
#define APPARENT_POWER_H

#include "esp_err.h"

/**
 * @brief Inicializa el modulo de potencia aparente.
 * @param target_vacc Constante [uVA/code].
 * @param target_vahcc Constante [nVAh/code].
 * @return ESP_OK en caso de exito.
 */
esp_err_t apparent_power_init(float target_vacc, float target_vahcc);

/**
 * @brief Obtiene potencia aparente instantanea.
 * @param void Sin parametros.
 * @return Potencia aparente en VA.
 */
float apparent_power_get_va(void);

/**
 * @brief Obtiene energia aparente acumulada.
 * @param void Sin parametros.
 * @return Energia aparente en VAh.
 */
float apparent_power_get_vah(void);

#endif // APPARENT_POWER_H
