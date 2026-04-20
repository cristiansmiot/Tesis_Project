#ifndef OLED_DRIVER_H
#define OLED_DRIVER_H

#include <stdint.h>
#include "esp_err.h"

#define OLED_DRIVER_MAX_ROWS 8U
#define OLED_DRIVER_MAX_COLS 32U

/**
 * @brief Inicializa OLED SSD1306 sobre I2C.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t oled_driver_init(void);

/**
 * @brief Limpia el framebuffer del OLED.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t oled_driver_clear(void);

/**
 * @brief Dibuja una linea de texto ASCII simple sobre una fila del display.
 * @param row Fila logica [0..7].
 * @param text Texto a mostrar.
 * @return ESP_OK en caso de exito.
 */
esp_err_t oled_driver_draw_text(uint8_t row, const char *text);

/**
 * @brief Env?a el framebuffer actual al display.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t oled_driver_flush(void);

#endif // OLED_DRIVER_H
