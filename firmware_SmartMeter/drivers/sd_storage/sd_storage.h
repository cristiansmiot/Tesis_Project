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
 * @brief Inicializa o reutiliza SPI2_HOST compartido y monta la SD con FATFS.
 *
 * Comparte MOSI/MISO/SCLK con el ADE9153A y usa un CS independiente
 * (ver board_config.h, SD_PIN_*). El driver SPI serializa transacciones.
 * El punto de montaje queda expuesto en SD_MOUNT_POINT ("/sdcard").
 *
 * Es idempotente: llamadas subsiguientes devuelven ESP_OK si ya esta
 * montada.
 *
 * @return ESP_OK si la tarjeta se monto y responde.
 */
esp_err_t sd_storage_init(void);

/** Desmonta FATFS de forma idempotente. No libera el bus SPI compartido. */
esp_err_t sd_storage_deinit(void);

/**
 * Invalida el montaje despues de un error de E/S. El siguiente intento del
 * servicio debe desmontar/remontar antes de volver a operar.
 */
void sd_storage_invalidate(void);

/** @return true si sd_storage_init() monto FAT exitosamente. */
bool sd_storage_is_mounted(void);

/**
 * @brief Escribe (append) una linea de texto a un archivo en la SD.
 *
 * Crea el archivo si no existe. Agrega '\n' automaticamente si la linea
 * no lo trae. Seguro para llamar desde cualquier tarea: el driver serializa
 * append, consulta de espacio y drenado completo del backlog con un mutex
 * propio, ademas de la proteccion interna de FATFS.
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

/** Crea un directorio relativo si aun no existe. */
esp_err_t sd_storage_ensure_dir(const char *relpath);

/** Recupera un reemplazo interrumpido (.tmp/.bak) para una ruta relativa. */
esp_err_t sd_storage_recover_file(const char *relpath);

/** Lee la primera linea completa sin retirarla. */
esp_err_t sd_storage_peek_line(const char *relpath,
                               char *out,
                               size_t out_len);

/** Retira de forma recuperable la primera linea de un archivo. */
esp_err_t sd_storage_drop_first_line(const char *relpath,
                                     size_t *out_remaining);

/**
 * Retira hasta max_drop lineas despues de preserve_lines mediante reemplazo
 * recuperable .tmp/.bak. Se usa para retencion acotada: por ejemplo,
 * preserve_lines=1 conserva el encabezado CSV y descarta datos antiguos.
 */
esp_err_t sd_storage_trim_oldest_lines(const char *relpath,
                                       size_t preserve_lines,
                                       size_t max_drop,
                                       size_t *out_dropped,
                                       uint64_t *out_dropped_bytes,
                                       size_t *out_remaining);

/** Elimina un archivo relativo. ESP_ERR_NOT_FOUND si no existe. */
esp_err_t sd_storage_remove_file(const char *relpath);

/** Consulta tamano de archivo en bytes. */
esp_err_t sd_storage_get_file_size(const char *relpath, uint64_t *out_size);

/**
 * Callback para drenar lineas pendientes. Devuelve ESP_OK si la linea se
 * proceso (p.ej. se publico por MQTT) y puede descartarse del archivo;
 * cualquier otro valor detiene el drenado y conserva la linea.
 */
typedef esp_err_t (*sd_line_consumer_t)(const char *line, void *ctx);

/**
 * @brief Consume hasta max_lines del inicio de un archivo de backlog.
 *
 * Lee linea por linea invocando al consumidor; las lineas consumidas se
 * eliminan reescribiendo el remanente a un archivo temporal que reemplaza
 * al original (la SD no permite truncar por el frente). Si el consumidor
 * falla a mitad, lo ya consumido no se repite y el resto queda en el
 * archivo para el siguiente intento.
 *
 * @param relpath       Ruta relativa al mount (p.ej. "backlog.jsonl").
 * @param max_lines     Tope de lineas a consumir en esta llamada.
 * @param consumer      Callback que procesa cada linea.
 * @param ctx           Contexto opaco que se pasa al callback.
 * @param out_remaining Lineas que quedaron pendientes (puede ser NULL).
 * @return ESP_OK si proceso al menos cero lineas sin error de E/S;
 *         ESP_ERR_NOT_FOUND si el archivo no existe (backlog vacio).
 */
esp_err_t sd_storage_drain_lines(const char *relpath,
                                 size_t max_lines,
                                 sd_line_consumer_t consumer,
                                 void *ctx,
                                 size_t *out_remaining);

#ifdef __cplusplus
}
#endif

#endif // SD_STORAGE_H
