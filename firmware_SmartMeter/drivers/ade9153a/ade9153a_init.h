#ifndef ADE9153A_INIT_H
#define ADE9153A_INIT_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    ADE9153A_RECOVERY_ACTION_PROBE = 0,
    ADE9153A_RECOVERY_ACTION_SOFT_INIT = 1,
    ADE9153A_RECOVERY_ACTION_SPI_REINIT = 2,
} ade9153a_recovery_action_t;

typedef struct {
    bool initialized;
    bool ready;
    bool bus_disconnected;
    esp_err_t probe_err;
    uint16_t run;
    uint16_t version;
    uint32_t product;
    uint32_t chip_hi;
    uint32_t chip_lo;
    uint32_t recovery_count;
    ade9153a_recovery_action_t last_action;
} ade9153a_diag_t;

/**
 * @brief Inicializa ADE9153A con secuencia de reset y configuracion base.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_init(void);

/**
 * @brief Ejecuta reset hardware del ADE9153A.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_reset(void);

/**
 * @brief Arranca el DSP de metrologia.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_start_dsp(void);

/**
 * @brief Configura PGA gain para canales A y B.
 * @param gain_a Valor de registro para AI_PGAGAIN.
 * @param gain_b Valor de registro para BI_PGAGAIN.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_set_pgagain(uint8_t gain_a, uint8_t gain_b);

/**
 * @brief Configura frecuencia de red.
 * @param freq_hz Frecuencia (50 o 60 Hz).
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_set_line_freq(uint8_t freq_hz);

/**
 * @brief Configura VLEVEL para calculo reactivo.
 * @param vnom Voltaje nominal RMS.
 * @param vheadroom Headroom de voltaje.
 * @return ESP_OK en caso de exito.
 */
esp_err_t ade9153a_set_vlevel(float vnom, float vheadroom);

/**
 * @brief Verifica respuesta basica del chip.
 * @param void Sin parametros.
 * @return true si responde correctamente.
 */
bool ade9153a_is_ready(void);

/**
 * @brief Lee diagnostico completo del ADE9153A sin generar spam de logs.
 * @param out Estructura de salida.
 * @return ESP_OK si pudo sondear el bus.
 */
esp_err_t ade9153a_get_diag(ade9153a_diag_t *out);

/**
 * @brief Ejecuta una accion de recuperacion/probe sobre el ADE9153A.
 * @param action Tipo de accion.
 * @return ESP_OK si el chip queda listo tras la accion.
 */
esp_err_t ade9153a_recover(ade9153a_recovery_action_t action);

#endif // ADE9153A_INIT_H
