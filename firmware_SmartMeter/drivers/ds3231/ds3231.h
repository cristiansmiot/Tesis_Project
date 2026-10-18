#ifndef DS3231_H
#define DS3231_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool ready;
    bool oscillator_stopped;
    bool eosc_set;
    bool time_valid;
    time_t epoch_utc;
} ds3231_status_t;

/**
 * @brief Inicializa el RTC DS3231 sobre el bus I2C compartido con OLED.
 *
 * Verifica que el chip responde y que EOSC permanece en cero. OSF no se
 * limpia durante init: se conserva como evidencia hasta escribir y releer
 * una hora autoritativa valida. Es seguro llamarlo multiples veces.
 *
 * @return ESP_OK si el RTC responde.
 */
esp_err_t ds3231_init(void);

/** @return true si ds3231_init() ya completo exitosamente. */
bool ds3231_is_ready(void);

/** Estado del dispositivo y de la fecha almacenada. */
esp_err_t ds3231_get_status(ds3231_status_t *out_status);

/** Fuerza un nuevo probe en la siguiente inicializacion. */
void ds3231_invalidate(void);

/**
 * @brief Lee la hora actual del RTC en formato UTC.
 * @param out_tm Puntero a struct tm donde se escribe la hora.
 * @return ESP_OK en exito.
 */
esp_err_t ds3231_get_time(struct tm *out_tm);

/**
 * @brief Fija la hora del RTC (p.ej. desde NTP/CCLK del SIM7080G).
 * @param tm Struct tm con la hora en UTC.
 * @return ESP_OK en exito.
 */
esp_err_t ds3231_set_time(const struct tm *tm);

/**
 * @brief Lee la hora del RTC y la devuelve como epoch UTC.
 * @param out_epoch Puntero donde se escribe la hora (segundos desde 1970).
 * @return ESP_OK en exito.
 */
esp_err_t ds3231_get_epoch(time_t *out_epoch);

/**
 * @brief Lee temperatura interna del cristal (util para compensacion).
 * @param out_c Temperatura en grados Celsius (resolucion 0.25 C).
 * @return ESP_OK en exito.
 */
esp_err_t ds3231_get_temperature(float *out_c);

/** Convierte un calendario UTC a epoch sin depender de la TZ global. */
esp_err_t ds3231_tm_to_epoch_utc(const struct tm *tm, time_t *out_epoch);

#ifdef __cplusplus
}
#endif

#endif // DS3231_H
