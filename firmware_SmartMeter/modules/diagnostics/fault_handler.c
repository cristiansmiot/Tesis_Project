#include "fault_handler.h"

#include <stddef.h>
#include <string.h>
#include "logger.h"

static const char *TAG = "fault_handler";
static FaultCode_t s_last_fault = FAULT_NONE;
static uint32_t s_fault_counts[FAULT_CODE_MAX + 1];

/**
 * @brief Inicializa estado interno de fallas.
 * @return ESP_OK siempre.
 */
esp_err_t fault_handler_init(void)
{
    s_last_fault = FAULT_NONE;
    (void)memset(s_fault_counts, 0, sizeof(s_fault_counts));
    return ESP_OK;
}

/**
 * @brief Registra una falla y acumula contador.
 * @param code Codigo de falla.
 * @param context Contexto opcional.
 */
void fault_set(FaultCode_t code, const char *context)
{
    if ((code < FAULT_NONE) || (code > FAULT_CODE_MAX)) {
        LOG_WARN(TAG, "invalid fault code: %d", (int)code);
        return;
    }

    s_last_fault = code;
    s_fault_counts[(int)code]++;
    if (context != NULL) {
        LOG_ERROR(TAG, "fault=%d context=%s", (int)code, context);
    } else {
        LOG_ERROR(TAG, "fault=%d", (int)code);
    }
}

/**
 * @brief Retorna la ultima falla.
 * @return Codigo de falla.
 */
FaultCode_t fault_get_last(void)
{
    return s_last_fault;
}

/**
 * @brief Retorna contador de una falla.
 * @param code Codigo consultado.
 * @return Cantidad acumulada.
 */
uint32_t fault_get_count(FaultCode_t code)
{
    if ((code < FAULT_NONE) || (code > FAULT_CODE_MAX)) {
        return 0U;
    }
    return s_fault_counts[(int)code];
}

/**
 * @brief Limpia ultima falla registrada.
 */
void fault_clear_last(void)
{
    s_last_fault = FAULT_NONE;
}

/**
 * @brief Evalua si existe alguna falla critica acumulada.
 * @return true si hay fallas criticas.
 */
bool fault_has_critical(void)
{
    if (s_fault_counts[FAULT_SPI] > 0U) {
        return true;
    }
    if (s_fault_counts[FAULT_INIT] > 0U) {
        return true;
    }
    if (s_fault_counts[FAULT_OVERTEMP] > 0U) {
        return true;
    }
    return false;
}
