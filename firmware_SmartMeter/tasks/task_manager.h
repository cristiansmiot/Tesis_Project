#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define METER_EVENT_NEW_DATA      1U
#define METER_EVENT_FAULT         2U
#define CALIB_REQUEST_FULL        1U
#define CALIB_REQUEST_CHANNEL_A   2U
#define CALIB_REQUEST_CHANNEL_A_TURBO 3U

typedef struct {
    int64_t timestamp_us;
    uint8_t event_type;
} MeterEvent_t;

typedef struct {
    uint8_t request_type;
} CalibRequest_t;

extern QueueHandle_t q_meter_events;
extern QueueHandle_t q_calib_request;
extern QueueHandle_t q_pq_alerts;

/**
 * @brief Inicializa colas globales del sistema de tareas.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t task_manager_init(void);

/**
 * @brief Crea y arranca tareas activas del sprint.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t task_manager_start(void);

/**
 * @brief Solicita calibracion via cola.
 * @param request_type Tipo de calibracion solicitada.
 * @return void
 */
void task_manager_request_calibration(uint8_t request_type);

#endif // TASK_MANAGER_H
