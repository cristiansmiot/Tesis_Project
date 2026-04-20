#ifndef FAULT_HANDLER_H
#define FAULT_HANDLER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    FAULT_NONE = 0,
    FAULT_SPI = 1,
    FAULT_ADC = 2,
    FAULT_OVERTEMP = 3,
    FAULT_INIT = 4,
    FAULT_MUTEX = 5,
} FaultCode_t;

/**
 * @brief Inicializa el manejador de fallas.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t fault_handler_init(void);

/**
 * @brief Registra una falla.
 * @param code Codigo de falla.
 * @param context Contexto textual opcional.
 * @return void
 */
void fault_set(FaultCode_t code, const char *context);

/**
 * @brief Obtiene la ultima falla registrada.
 * @param void Sin parametros.
 * @return Codigo de falla.
 */
FaultCode_t fault_get_last(void);

/**
 * @brief Obtiene contador de ocurrencias de una falla.
 * @param code Codigo de falla.
 * @return Cantidad acumulada.
 */
uint32_t fault_get_count(FaultCode_t code);

/**
 * @brief Limpia el estado de ultima falla.
 * @param void Sin parametros.
 * @return void
 */
void fault_clear_last(void);

/**
 * @brief Indica si hubo falla critica.
 * @param void Sin parametros.
 * @return true si existe alguna falla critica registrada.
 */
bool fault_has_critical(void);

#endif // FAULT_HANDLER_H
