#include "task_monitor.h"

#include <string.h>
#include "esp_system.h"
#include "esp_timer.h"
#include "fault_handler.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger.h"
#include "meter_config.h"
#include "rtos_app_config.h"

static const char *TAG = "task_monitor";

typedef struct {
    const char *name;
    uint32_t timeout_ms;
    int64_t last_beat_us;     // esp_timer_get_time() del ultimo latido
    int64_t stalled_since_us; // 0 si no esta bloqueada
    TaskHandle_t handle;      // capturado en el primer latido de la tarea
    bool registered;
    bool stalled;
} TaskMonSlot_t;

static TaskMonSlot_t s_slots[TASK_MON_COUNT];
static uint32_t s_stall_events;
static TaskHandle_t s_supervisor_handle;

esp_err_t task_monitor_init(void)
{
    (void)memset(s_slots, 0, sizeof(s_slots));
    s_stall_events = 0U;
    return ESP_OK;
}

void task_monitor_register(TaskMonId_t id, const char *name, uint32_t timeout_ms)
{
    if (id >= TASK_MON_COUNT) {
        return;
    }
    s_slots[id].name = (name != NULL) ? name : "?";
    s_slots[id].timeout_ms = timeout_ms;
    s_slots[id].last_beat_us = esp_timer_get_time();
    s_slots[id].stalled_since_us = 0;
    s_slots[id].registered = true;
    s_slots[id].stalled = false;
}

void task_monitor_heartbeat(TaskMonId_t id)
{
    if (id >= TASK_MON_COUNT) {
        return;
    }
    // Escritura de 64 bits sin mutex: el supervisor corre cada varios
    // segundos y tolera leer un valor a medio actualizar (solo causaria
    // un falso positivo/negativo transitorio de un ciclo, sin efectos).
    s_slots[id].last_beat_us = esp_timer_get_time();

    // El latido llega desde la propia tarea: primer punto donde se conoce
    // su handle sin acoplar el registro (task_manager no ve los handles
    // estaticos de cada task_*_start).
    if (s_slots[id].handle == NULL) {
        s_slots[id].handle = xTaskGetCurrentTaskHandle();
    }
}

bool task_monitor_is_stalled(TaskMonId_t id)
{
    if (id >= TASK_MON_COUNT) {
        return false;
    }
    return s_slots[id].stalled;
}

uint32_t task_monitor_get_stall_events(void)
{
    return s_stall_events;
}

static void task_monitor_check_slot(TaskMonSlot_t *slot, int64_t now_us)
{
    if (!slot->registered) {
        return;
    }

    const int64_t silence_us = now_us - slot->last_beat_us;
    const int64_t timeout_us = (int64_t)slot->timeout_ms * 1000LL;

    if (silence_us > timeout_us) {
        if (!slot->stalled) {
            slot->stalled = true;
            slot->stalled_since_us = now_us;
            ++s_stall_events;
            fault_set(FAULT_TASK_STALL, slot->name);
            LOG_ERROR(TAG, "tarea '%s' sin latido hace %lld ms (umbral %lu ms)",
                      slot->name, (long long)(silence_us / 1000LL),
                      (unsigned long)slot->timeout_ms);
        } else {
            // Escalamiento: si la tarea sigue muerta tras el plazo de
            // gracia, el reinicio completo es preferible a un nodo que
            // aparenta estar vivo pero no publica ni mide.
            const int64_t stalled_us = now_us - slot->stalled_since_us;
            if (stalled_us > ((int64_t)METER_TASK_STALL_REBOOT_MS * 1000LL)) {
                LOG_ERROR(TAG, "tarea '%s' bloqueada %lld s - reiniciando nodo",
                          slot->name, (long long)(stalled_us / 1000000LL));
                vTaskDelay(pdMS_TO_TICKS(200)); // tiempo para drenar el log
                esp_restart();
            }
        }
    } else if (slot->stalled) {
        slot->stalled = false;
        slot->stalled_since_us = 0;
        LOG_WARN(TAG, "tarea '%s' recupero el latido", slot->name);
    }
}

/**
 * Reporta la marca de agua de stack de las tareas supervisadas. La marca
 * es el minimo historico de palabras libres: si se acerca a cero, la
 * tarea esta a un pico de actividad de desbordar y corromper memoria.
 * Ajustar TASK_STACK_* en rtos_app_config.h con estos datos en vez de
 * dimensionar a ojo.
 */
static void task_monitor_log_stacks(void)
{
    for (int i = 0; i < (int)TASK_MON_COUNT; ++i) {
        if (!s_slots[i].registered || (s_slots[i].handle == NULL)) {
            continue;
        }
        const UBaseType_t libres = uxTaskGetStackHighWaterMark(s_slots[i].handle);
        if (libres < METER_TASK_STACK_WARN_WORDS) {
            LOG_WARN(TAG, "stack de '%s' al limite: %u palabras libres",
                     s_slots[i].name, (unsigned)libres);
        } else {
            LOG_INFO(TAG, "stack '%s': %u palabras libres (minimo historico)",
                     s_slots[i].name, (unsigned)libres);
        }
    }
}

static void task_supervisor(void *pvParameters)
{
    (void)pvParameters;
    LOG_INFO(TAG, "supervisor activo (revision cada %u ms)",
             (unsigned)METER_TASK_MONITOR_PERIOD_MS);

    uint32_t ciclos = 0U;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(METER_TASK_MONITOR_PERIOD_MS));
        const int64_t now_us = esp_timer_get_time();
        for (int i = 0; i < (int)TASK_MON_COUNT; ++i) {
            task_monitor_check_slot(&s_slots[i], now_us);
        }

        // Reporte de stacks cada N revisiones (~5 min con periodo de 5 s):
        // suficiente para calibrar tamanos sin inundar el log.
        ++ciclos;
        if ((ciclos % METER_TASK_STACK_LOG_EVERY) == 0U) {
            task_monitor_log_stacks();
        }
    }
}

esp_err_t task_monitor_start(void)
{
    if (s_supervisor_handle != NULL) {
        return ESP_OK;
    }

    const BaseType_t rc = xTaskCreatePinnedToCore(
        task_supervisor,
        "task_supervisor",
        TASK_STACK_SUPERVISOR,
        NULL,
        TASK_PRIO_SUPERVISOR,
        &s_supervisor_handle,
        tskNO_AFFINITY);

    if (rc != pdPASS) {
        LOG_ERROR(TAG, "no se pudo crear el supervisor");
        s_supervisor_handle = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}
