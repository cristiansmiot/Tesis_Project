#ifndef KEYPAD_DRIVER_H
#define KEYPAD_DRIVER_H

#include "esp_err.h"

typedef enum {
    KEYPAD_EVENT_NONE = 0,
    KEYPAD_EVENT_UP,
    KEYPAD_EVENT_DOWN,
    KEYPAD_EVENT_SELECT,
    KEYPAD_EVENT_BACK,
} keypad_event_t;

/**
 * @brief Inicializa los GPIO de navegacion.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t keypad_driver_init(void);

/**
 * @brief Sondea botones con debounce y genera un evento discreto.
 * @param void Sin parametros.
 * @return Evento detectado o KEYPAD_EVENT_NONE.
 */
keypad_event_t keypad_driver_poll_event(void);

#endif // KEYPAD_DRIVER_H
