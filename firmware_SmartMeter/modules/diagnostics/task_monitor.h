#ifndef TASK_MONITOR_H
#define TASK_MONITOR_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Watchdog por software a nivel de tarea.
 *
 * El task watchdog de ESP-IDF (esp_task_wdt) reinicia el chip completo
 * cuando una tarea deja de alimentarlo. Aqui se busca lo contrario: que
 * el bloqueo de una tarea (p.ej. la de comunicacion atascada en un AT
 * del SIM7080G) quede detectado y reportado sin tumbar la medicion ni
 * la UI. Cada tarea registrada reporta latidos; una tarea supervisora
 * de baja prioridad detecta latidos vencidos, los registra como falla
 * (FAULT_TASK_STALL) y solo escala a esp_restart() si la tarea sigue
 * bloqueada mas alla de METER_TASK_STALL_REBOOT_MS.
 */

typedef enum {
    TASK_MON_MEASUREMENT = 0,
    TASK_MON_POWER_QUALITY,
    TASK_MON_COMMUNICATION,
    TASK_MON_UI,
    TASK_MON_COUNT,
} TaskMonId_t;

/** Inicializa la tabla de latidos. Llamar antes de registrar tareas. */
esp_err_t task_monitor_init(void);

/**
 * @brief Registra una tarea a supervisar.
 * @param id         Identificador de la tarea.
 * @param name       Nombre corto para logs.
 * @param timeout_ms Tiempo maximo tolerado entre latidos. Debe ser
 *                   holgado frente al peor caso del ciclo de la tarea
 *                   (p.ej. la de comunicacion puede tardar minutos en
 *                   un recovery completo del modem).
 */
void task_monitor_register(TaskMonId_t id, const char *name, uint32_t timeout_ms);

/** Reporta latido. Llamar una vez por ciclo del lazo de la tarea. */
void task_monitor_heartbeat(TaskMonId_t id);

/** Crea la tarea supervisora. Llamar despues de registrar las tareas. */
esp_err_t task_monitor_start(void);

/** @return true si la tarea tiene el latido vencido en este momento. */
bool task_monitor_is_stalled(TaskMonId_t id);

/** @return Total acumulado de eventos de bloqueo detectados desde boot. */
uint32_t task_monitor_get_stall_events(void);

#ifdef __cplusplus
}
#endif

#endif // TASK_MONITOR_H
