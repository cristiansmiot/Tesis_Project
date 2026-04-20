#ifndef RTOS_APP_CONFIG_H
#define RTOS_APP_CONFIG_H

// Prioridades de tareas
#define TASK_PRIO_MEASUREMENT    5
#define TASK_PRIO_POWER_QUALITY  4
#define TASK_PRIO_CALIBRATION    3
#define TASK_PRIO_COMMUNICATION  2
#define TASK_PRIO_UI             1
#define TASK_PRIO_POWER_MGMT     1

// Stack sizes (palabras de FreeRTOS)
#define TASK_STACK_MEASUREMENT   4096
#define TASK_STACK_CALIBRATION   8192
#define TASK_STACK_PQ            4096
#define TASK_STACK_COMM          6144
#define TASK_STACK_UI            5120
#define TASK_STACK_POWER_MGMT    4096

// Longitudes de queues
#define QUEUE_METER_EVENTS_LEN   16
#define QUEUE_CALIB_REQUEST_LEN  4
#define QUEUE_PQ_ALERTS_LEN      16

// Feature flags de Sprint 1+
// 0 = no publicar eventos en q_meter_events
// 1 = publicar eventos para consumidores como la UI
#define FEATURE_METER_EVENTS_PUBLISH 1

// Timeouts
#define TIMEOUT_MUTEX_MS         10
#define TIMEOUT_QUEUE_SEND_MS    0
#define TIMEOUT_CALIB_TOTAL_MS   60000

#endif // RTOS_APP_CONFIG_H
