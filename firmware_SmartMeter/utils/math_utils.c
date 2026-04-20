#include "math_utils.h"

/**
 * @brief Limita un valor flotante en un rango.
 * @param value Valor a limitar.
 * @param min_value Limite inferior.
 * @param max_value Limite superior.
 * @return Valor limitado al rango [min_value, max_value].
 */
float math_utils_clampf(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

/**
 * @brief Convierte una razon en porcentaje.
 * @param ratio Razon en escala 0..1.
 * @return Valor en porcentaje.
 */
float math_utils_ratio_to_percent(float ratio)
{
    return ratio * 100.0f;
}
