#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa bus SPI3_HOST dedicado y monta la SD con FATFS.
 *
 * Usa pines aislados del ADE9153A (ver board_config.h, SD_PIN_*) para
 * evitar cualquier impacto en el muestreo a 4 kHz del metrologia.
 * El punto de montaje queda expuesto en SD_MOUNT_POINT ("/sdcard").
 *
 * Es idempotente: llamadas subsiguientes devuelven ESP_OK si ya esta
 * montada.
 *
 * @return ESP_OK si la tarjeta se monto y responde.
 */
esp_err_t sd_storage_init(void);

/** @return true si sd_storage_init() monto FAT exitosamente. */
bool sd_storage_is_mounted(void);

/**
 * @brief Escribe (append) una linea de texto a un archivo en la SD.
 *
 * Crea el archivo si no existe. Agrega '\n' automaticamente si la linea
 * no lo trae. Seguro para llamar desde cualquier tarea (FATFS ya usa
 * mutex interno).
 *
 * @param relpath Ruta relativa al mount (p.ej. "mediciones.jsonl").
 *                No debe incluir el prefijo "/sdcard/".
 * @param line    Cadena a escribir.
 * @return ESP_OK en exito.
 */
esp_err_t sd_storage_append_line(const char *relpath, const char *line);

/**
 * @brief Reporta capacidad de la SD montada.
 * @param out_total_mb Capacidad total en MB (puede ser NULL).
 * @param out_free_mb  Espacio libre en MB (puede ser NULL).
 * @return ESP_OK en exito.
 */
esp_err_t sd_storage_get_usage(uint32_t *out_total_mb, uint32_t *out_free_mb);

#ifdef __cplusplus
}
#endif

#endif // SD_STORAGE_H
