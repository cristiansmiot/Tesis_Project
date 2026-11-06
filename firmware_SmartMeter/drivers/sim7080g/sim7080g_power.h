#ifndef SIM7080G_POWER_H
#define SIM7080G_POWER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool initialized;
    uint8_t consecutive_no_at;
    uint8_t pulse_attempts;
    uint32_t pulses_total;
    int64_t last_pulse_uptime_us;
    bool cold_boot;          /**< rc4: ESP_RST_POWERON (el HAT tambien perdio energia). */
    uint32_t hard_cycles_total;
} sim7080g_power_snapshot_t;

/** Configura PWR del HAT como salida activa-alta e inactiva en LOW. */
esp_err_t sim7080g_power_init(void);

/**
 * Registra un fallo real de UART y, cuando corresponde segun la politica,
 * genera un pulso PWRKEY y espera el tiempo de arranque del modem.
 *
 * ESP_OK significa que se genero el pulso. ESP_ERR_NOT_FINISHED indica que
 * aun faltan pruebas UART. ESP_ERR_TIMEOUT indica que rige el cooldown.
 */
esp_err_t sim7080g_power_handle_no_at(bool *pulsed);

/** Reinicia los contadores de la averia cuando el modem responde AT. */
void sim7080g_power_note_at_ready(void);

/**
 * rc4, N3 de la escalera: reinicio fisico con el modem contestando AT.
 * AT+CPOWD=1 (apagado ordenado); si sigue contestando, pulso PWRKEY para
 * apagarlo. Confirmado el silencio, pulso PWRKEY para encenderlo y espera a
 * que conteste AT (un segundo pulso si no vuelve). No usa la politica NO_AT.
 */
esp_err_t sim7080g_power_hard_cycle(void);

/**
 * Apaga el modem si contesta AT (AT+CPOWD=1; pulso PWRKEY si sigue
 * encendido). Para perfiles sin comunicaciones. ESP_OK si queda apagado.
 */
esp_err_t sim7080g_power_off_if_on(void);

/** Obtiene contadores diagnosticos sin memoria dinamica. */
esp_err_t sim7080g_power_get_snapshot(sim7080g_power_snapshot_t *out);

#endif /* SIM7080G_POWER_H */
