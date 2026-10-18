#include "fault_handler.h"

#include <stddef.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "logger.h"
#include "system_health.h"

static const char *TAG = "fault_handler";
static FaultCode_t s_last_fault = FAULT_NONE;
static uint32_t s_fault_counts[FAULT_CODE_MAX + 1];
static system_module_t s_last_module = SYSTEM_MODULE_SYSTEM;
static portMUX_TYPE s_fault_lock = portMUX_INITIALIZER_UNLOCKED;

static void fault_map(FaultCode_t code,
                      system_module_t *module,
                      system_event_code_t *event,
                      system_severity_t *severity)
{
    *module = SYSTEM_MODULE_SYSTEM;
    *event = SYSTEM_EVENT_INIT_FAILED;
    *severity = SYSTEM_SEVERITY_WARNING;
    switch (code) {
        case FAULT_SPI:
            *module = SYSTEM_MODULE_ADE;
            *event = SYSTEM_EVENT_SPI_ERROR;
            break;
        case FAULT_ADC:
            *module = SYSTEM_MODULE_ADE;
            *event = SYSTEM_EVENT_ADC_ERROR;
            break;
        case FAULT_OVERTEMP:
            *module = SYSTEM_MODULE_PQ;
            *event = SYSTEM_EVENT_OVERTEMP;
            *severity = SYSTEM_SEVERITY_CRITICAL;
            break;
        case FAULT_MUTEX:
            *event = SYSTEM_EVENT_MUTEX_ERROR;
            *severity = SYSTEM_SEVERITY_CRITICAL;
            break;
        case FAULT_TASK_STALL:
            *event = SYSTEM_EVENT_TASK_STALL;
            *severity = SYSTEM_SEVERITY_CRITICAL;
            break;
        case FAULT_INIT:
        case FAULT_NONE:
        default:
            break;
    }
}

/**
 * @brief Inicializa estado interno de fallas.
 * @return ESP_OK siempre.
 */
esp_err_t fault_handler_init(void)
{
    const esp_err_t health_err = system_health_init();
    if (health_err != ESP_OK) {
        return health_err;
    }
    portENTER_CRITICAL(&s_fault_lock);
    s_last_fault = FAULT_NONE;
    s_last_module = SYSTEM_MODULE_SYSTEM;
    (void)memset(s_fault_counts, 0, sizeof(s_fault_counts));
    portEXIT_CRITICAL(&s_fault_lock);
    return ESP_OK;
}

/**
 * @brief Registra una falla y acumula contador.
 * @param code Codigo de falla.
 * @param context Contexto opcional.
 */
void fault_set(FaultCode_t code, const char *context)
{
    if ((code <= FAULT_NONE) || (code > FAULT_CODE_MAX)) {
        LOG_WARN(TAG, "invalid fault code: %d", (int)code);
        return;
    }

    system_module_t module;
    system_event_code_t event;
    system_severity_t severity;
    fault_map(code, &module, &event, &severity);

    portENTER_CRITICAL(&s_fault_lock);
    s_last_fault = code;
    s_last_module = module;
    s_fault_counts[(int)code]++;
    portEXIT_CRITICAL(&s_fault_lock);

    (void)system_health_report_failure(module, event, severity,
                                       ESP_FAIL, context);
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
    portENTER_CRITICAL(&s_fault_lock);
    const FaultCode_t value = s_last_fault;
    portEXIT_CRITICAL(&s_fault_lock);
    return value;
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
    portENTER_CRITICAL(&s_fault_lock);
    const uint32_t value = s_fault_counts[(int)code];
    portEXIT_CRITICAL(&s_fault_lock);
    return value;
}

/**
 * @brief Limpia ultima falla registrada.
 */
void fault_clear_last(void)
{
    portENTER_CRITICAL(&s_fault_lock);
    const system_module_t module = s_last_module;
    s_last_fault = FAULT_NONE;
    s_last_module = SYSTEM_MODULE_SYSTEM;
    portEXIT_CRITICAL(&s_fault_lock);
    (void)system_health_report_ok(module, "legacy fault cleared");
}

/**
 * @brief Evalua si existe alguna falla critica acumulada.
 * @return true si hay fallas criticas.
 */
bool fault_has_critical(void)
{
    return system_health_has_active_critical();
}
