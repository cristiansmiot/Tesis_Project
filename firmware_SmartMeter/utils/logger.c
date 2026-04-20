#include "logger.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"

static LogLevel_t s_min_level = LOG_LEVEL_INFO;

/**
 * @brief Inicializa el logger de aplicacion.
 * @param void Sin parametros.
 * @return void
 */
void logger_init(void)
{
    s_min_level = LOG_LEVEL_INFO;
    esp_log_level_set("*", ESP_LOG_INFO);
}

/**
 * @brief Configura el nivel minimo del logger.
 * @param level Nivel minimo a emitir.
 * @return void
 */
void logger_set_level(LogLevel_t level)
{
    s_min_level = level;
}

/**
 * @brief Emite un mensaje formateado de logging.
 * @param level Nivel del mensaje.
 * @param tag Tag del modulo.
 * @param fmt Formato tipo printf.
 * @param ... Lista variable de argumentos.
 * @return void
 */
void logger_log(LogLevel_t level, const char *tag, const char *fmt, ...)
{
    if (level < s_min_level) {
        return;
    }

    char buffer[256];
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    switch (level) {
        case LOG_LEVEL_DEBUG:
            ESP_LOGD(tag, "%s", buffer);
            break;
        case LOG_LEVEL_INFO:
            ESP_LOGI(tag, "%s", buffer);
            break;
        case LOG_LEVEL_WARN:
            ESP_LOGW(tag, "%s", buffer);
            break;
        case LOG_LEVEL_ERROR:
        default:
            ESP_LOGE(tag, "%s", buffer);
            break;
    }
}
