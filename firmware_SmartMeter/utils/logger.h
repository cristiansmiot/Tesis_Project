#ifndef LOGGER_H
#define LOGGER_H

#include <stdarg.h>
#include "esp_err.h"

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_ERROR = 3
} LogLevel_t;

/**
 * @brief Inicializa el sistema de logging.
 */
void logger_init(void);

/**
 * @brief Ajusta el nivel minimo de logging.
 * @param level Nivel minimo a emitir.
 */
void logger_set_level(LogLevel_t level);

/**
 * @brief Emite un log si supera el filtro actual.
 * @param level Nivel del mensaje.
 * @param tag Etiqueta de modulo.
 * @param fmt Formato tipo printf.
 * @param ... Argumentos del formato.
 */
void logger_log(LogLevel_t level, const char *tag, const char *fmt, ...);

#define LOG_DEBUG(tag, fmt, ...) logger_log(LOG_LEVEL_DEBUG, (tag), (fmt), ##__VA_ARGS__)
#define LOG_INFO(tag, fmt, ...)  logger_log(LOG_LEVEL_INFO,  (tag), (fmt), ##__VA_ARGS__)
#define LOG_WARN(tag, fmt, ...)  logger_log(LOG_LEVEL_WARN,  (tag), (fmt), ##__VA_ARGS__)
#define LOG_ERROR(tag, fmt, ...) logger_log(LOG_LEVEL_ERROR, (tag), (fmt), ##__VA_ARGS__)

#endif // LOGGER_H
