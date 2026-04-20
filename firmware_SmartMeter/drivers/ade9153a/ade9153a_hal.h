#ifndef ADE9153A_HAL_H
#define ADE9153A_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    esp_err_t (*spi_transfer)(uint8_t *tx, uint8_t *rx, size_t len);
    void (*cs_set)(bool active);
    void (*reset_set)(bool active);
    void (*platform_delay_ms)(uint32_t ms);
} ADE9153A_HAL_t;

/**
 * @brief Inicializa la tabla HAL del driver ADE9153A.
 * @param hal Estructura con callbacks de plataforma.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_hal_init(ADE9153A_HAL_t *hal);

/**
 * @brief Retorna la tabla HAL activa.
 * @param void Sin parametros.
 * @return Puntero constante al HAL o NULL si no esta inicializado.
 */
const ADE9153A_HAL_t *ade9153a_hal_get(void);

#endif // ADE9153A_HAL_H
