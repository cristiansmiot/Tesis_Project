#ifndef TASK_UI_H
#define TASK_UI_H

#include "esp_err.h"

/**
 * @brief Crea y arranca la tarea de UI.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t task_ui_start(void);

#endif // TASK_UI_H
