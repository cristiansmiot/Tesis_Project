#ifndef TEMPERATURE_H
#define TEMPERATURE_H

#include "esp_err.h"

/**
 * @brief Inicializa el modulo de temperatura.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t temperature_init(void);

/**
 * @brief Fuerza una lectura de temperatura interna del ADE9153A.
 * @param out_temp_c Salida en grados Celsius.
 * @return ESP_OK si la muestra es valida.
 */
esp_err_t temperature_read_internal(float *out_temp_c);

/**
 * @brief Obtiene temperatura interna ADE9153A.
 * @param void Sin parametros.
 * @return Temperatura en grados Celsius; 0.0f si la lectura falla.
 */
float temperature_get_internal(void);

#endif // TEMPERATURE_H

