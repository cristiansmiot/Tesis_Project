#ifndef CURRENT_H
#define CURRENT_H

#include "esp_err.h"

/**
 * @brief Inicializa el modulo de corriente.
 * @param target_aicc Constante conversion canal A [nA/code].
 * @param target_bicc Constante conversion canal B [nA/code].
 * @return ESP_OK en caso de exito.
 */
esp_err_t current_init(float target_aicc, float target_bicc);

/**
 * @brief Obtiene corriente RMS canal A.
 * @param void Sin parametros.
 * @return Corriente RMS en amperes.
 */
float current_get_irms_a(void);

/**
 * @brief Obtiene corriente RMS canal B.
 * @param void Sin parametros.
 * @return Corriente RMS en amperes.
 */
float current_get_irms_b(void);

#endif // CURRENT_H
