#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include "esp_err.h"
#include "ui_types.h"

/**
 * @brief Inicializa el gestor de display.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t display_manager_init(void);

/**
 * @brief Renderiza el estado UI actual sobre el OLED.
 * @param state Estado de menu.
 * @param context Contexto de datos/diagnostico.
 * @return ESP_OK en caso de exito.
 */
esp_err_t display_manager_render(const ui_menu_state_t *state,
                                 const ui_display_context_t *context);

/**
 * @brief Muestra pantalla de splash al arranque.
 * @return ESP_OK en caso de exito.
 */
esp_err_t display_manager_show_splash(void);

#endif // DISPLAY_MANAGER_H
