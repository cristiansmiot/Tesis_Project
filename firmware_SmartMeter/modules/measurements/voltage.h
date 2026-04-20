#ifndef VOLTAGE_H
#define VOLTAGE_H

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief Inicializa el modulo de voltaje.
 * @param target_avcc Constante de conversion objetivo [nV/code].
 * @return ESP_OK en caso de exito.
 */
esp_err_t voltage_init(float target_avcc);

/**
 * @brief Obtiene voltaje RMS de fase A.
 * @param void Sin parametros.
 * @return Voltaje RMS en volts.
 */
float voltage_get_vrms(void);

/**
 * @brief Obtiene voltaje pico.
 * @param void Sin parametros.
 * @return Voltaje pico en volts.
 */
float voltage_get_vpeak(void);

/**
 * @brief Obtiene frecuencia de linea.
 * @param void Sin parametros.
 * @return Frecuencia en Hz.
 */
float voltage_get_frequency(void);

/**
 * @brief Obtiene AVRMS en formato crudo del registro (sin conversion fisica).
 * @param raw_out Puntero de salida para el valor crudo.
 * @return ESP_OK en caso de exito.
 */
esp_err_t voltage_get_vrms_raw(uint32_t *raw_out);

/**
 * @brief Obtiene APERIOD en formato crudo del registro.
 * @param raw_out Puntero de salida para el valor crudo.
 * @return ESP_OK en caso de exito.
 */
esp_err_t voltage_get_aperiod_raw(uint32_t *raw_out);

#endif // VOLTAGE_H
