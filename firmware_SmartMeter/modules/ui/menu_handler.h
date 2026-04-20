#ifndef MENU_HANDLER_H
#define MENU_HANDLER_H

#include "esp_err.h"
#include "keypad_driver.h"
#include "ui_types.h"

/**
 * @brief Inicializa el estado de navegacion del menu.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t menu_handler_init(void);

/**
 * @brief Procesa un evento de botones y actualiza el estado UI.
 * @param event Evento recibido.
 * @return void
 */
void menu_handler_process_event(keypad_event_t event);

/**
 * @brief Obtiene una copia del estado actual del menu.
 * @param out Estado de salida.
 * @return ESP_OK en caso de exito.
 */
esp_err_t menu_handler_get_state(ui_menu_state_t *out);

/**
 * @brief Consume una accion pendiente generada por la UI.
 * @param void Sin parametros.
 * @return Accion pendiente o UI_ACTION_NONE.
 */
ui_action_t menu_handler_take_pending_action(void);

/**
 * @brief Consume la bandera dirty para saber si debe re-renderizarse.
 * @param void Sin parametros.
 * @return true si habia cambios pendientes.
 */
bool menu_handler_consume_dirty(void);

/**
 * @brief Obtiene el nombre corto de una pantalla.
 * @param screen Pantalla consultada.
 * @return Cadena constante.
 */
const char *menu_handler_get_screen_name(ui_screen_t screen);

/**
 * @brief Verifica inactividad y transiciona a HOME si el idle supera el umbral.
 * @return true si transiciono a HOME.
 */
bool menu_handler_check_idle_home(void);

#endif // MENU_HANDLER_H
