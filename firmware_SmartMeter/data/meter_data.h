#ifndef METER_DATA_H
#define METER_DATA_H

#include <stdint.h>
#include "esp_err.h"

typedef struct {
    float vrms;                 // vrms: voltaje RMS publicado por MQTT.
    float vpeak;                // Pico de voltaje; hoy no esta validado completamente.
    float irms_a;               // irms_a: corriente RMS del canal A / CT principal.
    float irms_b;               // Canal B reservado/diagnostico segun hardware.
    float active_power_w;       // p_w: potencia activa real en watts.
    float reactive_power_var;   // q_var: potencia reactiva; el signo depende del cuadrante y la convencion elegida.
    float apparent_power_va;    // s_va: potencia aparente; se reporta como magnitud positiva.
    float energy_hw_wh;         // E_hw en log serie: acumulador hardware del ADE9153A.
    float energy_soft_wh;       // e_soft_wh: energia integrada por software desde p_w.
    float energy_total_nvs_wh;  // e_nvs_wh: total persistido en NVS.
    float energy_wh;            // e_wh: alias legacy del total publicado; hoy equivale a energy_total_nvs_wh.
    float energy_varh;          // Energia reactiva acumulada.
    float energy_vah;           // Energia aparente acumulada.
    float power_factor;         // pf: factor de potencia.
    float frequency;            // f_hz: frecuencia de linea.
    float temperature;          // temp_c: temperatura interna del ADE.
    uint8_t temperature_valid;  // temp_ok: 1 si la conversion TEMP_TRIM/TEMP_RSLT fue valida.
    uint32_t pq_status;         // pq: bitmask de power quality.
    uint8_t calibration_status; // cal/CAL: bitmask CALIB_STATUS_*.
    uint8_t ac_present;         // 1 si AC presente (latched con histeresis).
    int64_t timestamp_us;       // ts_us: timestamp monotono de la muestra.
} MeterData_t;

/**
 * @brief Inicializa almacenamiento compartido de mediciones.
 * @param void Sin parametros.
 * @return ESP_OK en caso de exito.
 */
esp_err_t meter_data_init(void);

/**
 * @brief Actualiza snapshot compartido.
 * @param new_data Estructura de entrada.
 * @return ESP_OK en caso de exito.
 */
esp_err_t meter_data_update(const MeterData_t *new_data);

/**
 * @brief Obtiene una copia atomica del snapshot actual.
 * @param void Sin parametros.
 * @return Copia de MeterData_t.
 */
MeterData_t meter_data_get_snapshot(void);

#endif // METER_DATA_H
