#ifndef MATH_UTILS_H
#define MATH_UTILS_H

/**
 * @brief Limita un valor flotante en un rango.
 * @param value Valor a limitar.
 * @param min_value Limite inferior.
 * @param max_value Limite superior.
 * @return Valor limitado al rango [min_value, max_value].
 */
float math_utils_clampf(float value, float min_value, float max_value);

/**
 * @brief Convierte una razon a porcentaje.
 * @param ratio Razon en escala 0..1.
 * @return Porcentaje equivalente.
 */
float math_utils_ratio_to_percent(float ratio);

#endif // MATH_UTILS_H
