#include "sim7080g_hal.h"

#include <string.h>
#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "logger.h"

static const char *TAG = "sim7080g_hal";
static sim7080g_hal_config_t s_cfg;
static SemaphoreHandle_t s_at_mutex;
static bool s_ready;

static esp_err_t sim7080g_hal_wait_response_locked(const char *expected,
                                                   char *response,
                                                   size_t response_len,
                                                   uint32_t timeout_ms)
{
    const int64_t start_us = esp_timer_get_time();
    const int64_t timeout_us = ((int64_t)timeout_ms) * 1000LL;
    char line[160];
    size_t line_len = 0U;
    size_t resp_len = 0U;

    if ((response != NULL) && (response_len > 0U)) {
        response[0] = '\0';
    }

    for (;;) {
        const int64_t elapsed_us = esp_timer_get_time() - start_us;
        if (elapsed_us >= timeout_us) {
            return ESP_ERR_TIMEOUT;
        }

        uint8_t ch = 0U;
        const int n = uart_read_bytes((uart_port_t)s_cfg.uart_num,
                                      &ch,
                                      1,
                                      pdMS_TO_TICKS(50));
        if (n <= 0) {
            continue;
        }

        if ((response != NULL) && (response_len > 1U) && (resp_len + 1U < response_len)) {
            response[resp_len++] = (char)ch;
            response[resp_len] = '\0';
            if ((expected != NULL) && (strstr(response, expected) != NULL)) {
                return ESP_OK;
            }
        } else if ((expected != NULL) && (strlen(expected) == 1U) && ((char)ch == expected[0])) {
            // Prompt-style tokens (ej. '>') suelen no venir en una linea completa.
            return ESP_OK;
        }

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            if (line_len == 0U) {
                continue;
            }
            line[line_len] = '\0';
            LOG_DEBUG(TAG, "< %s", line);

            if ((expected != NULL) && (strstr(line, expected) != NULL)) {
                return ESP_OK;
            }
            if (strcmp(line, "OK") == 0) {
                return (expected == NULL) ? ESP_OK : ESP_FAIL;
            }
            if (strstr(line, "ERROR") != NULL) {
                return ESP_FAIL;
            }

            line_len = 0U;
            continue;
        }

        if (line_len + 1U < sizeof(line)) {
            line[line_len++] = (char)ch;
        } else {
            line_len = 0U;
        }
    }
}

esp_err_t sim7080g_hal_read_urc(const char *prefix,
                                 char *out,
                                 size_t out_len,
                                 uint32_t timeout_ms)
{
    if (!s_ready || (prefix == NULL) || (out == NULL) || (out_len == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_at_mutex, pdMS_TO_TICKS(timeout_ms + 200U)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const int64_t start_us = esp_timer_get_time();
    const int64_t timeout_us = ((int64_t)timeout_ms) * 1000LL;
    char line[160];
    size_t line_len = 0U;
    esp_err_t result = ESP_ERR_NOT_FOUND;

    while ((esp_timer_get_time() - start_us) < timeout_us) {
        uint8_t ch = 0U;
        const int n = uart_read_bytes((uart_port_t)s_cfg.uart_num,
                                      &ch, 1, pdMS_TO_TICKS(20));
        if (n <= 0) {
            continue;
        }
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            if (line_len == 0U) {
                continue;
            }
            line[line_len] = '\0';
            if (strstr(line, prefix) != NULL) {
                (void)strncpy(out, line, out_len - 1U);
                out[out_len - 1U] = '\0';
                result = ESP_OK;
                break;
            }
            line_len = 0U;
            continue;
        }
        if (line_len + 1U < sizeof(line)) {
            line[line_len++] = (char)ch;
        } else {
            line_len = 0U;
        }
    }

    (void)xSemaphoreGive(s_at_mutex);
    return result;
}

bool sim7080g_hal_is_ready(void)
{
    return s_ready;
}

esp_err_t sim7080g_hal_init(const sim7080g_hal_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* If already initialized with same config, just flush and reuse. */
    if (s_ready &&
        (s_cfg.uart_num == cfg->uart_num) &&
        (s_cfg.tx_pin == cfg->tx_pin) &&
        (s_cfg.rx_pin == cfg->rx_pin) &&
        (s_cfg.baudrate == cfg->baudrate)) {
        (void)uart_flush_input((uart_port_t)s_cfg.uart_num);
        return ESP_OK;
    }

    /* If already initialized but baud rate changed, reconfigure without
       tearing down the UART driver (avoids ESP-IDF spinlock corruption). */
    if (s_ready && (s_cfg.uart_num == cfg->uart_num)) {
        s_cfg.baudrate = cfg->baudrate;
        (void)uart_flush_input((uart_port_t)s_cfg.uart_num);
        const esp_err_t baud_err = uart_set_baudrate((uart_port_t)s_cfg.uart_num,
                                                      s_cfg.baudrate);
        if (baud_err != ESP_OK) {
            LOG_WARN(TAG, "uart_set_baudrate failed: %s", esp_err_to_name(baud_err));
            return baud_err;
        }
        LOG_INFO(TAG, "HAL reconfigured baud=%lu", (unsigned long)s_cfg.baudrate);
        return ESP_OK;
    }

    /* Fresh initialization. */
    s_cfg = *cfg;

    if (s_at_mutex == NULL) {
        s_at_mutex = xSemaphoreCreateMutex();
        if (s_at_mutex == NULL) {
            LOG_ERROR(TAG, "mutex create failed");
            return ESP_FAIL;
        }
    }

    const uart_config_t uart_cfg = {
        .baud_rate = (int)s_cfg.baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
#if defined(UART_SCLK_DEFAULT)
        .source_clk = UART_SCLK_DEFAULT,
#elif defined(UART_SCLK_APB)
        .source_clk = UART_SCLK_APB,
#else
        .source_clk = UART_SCLK_XTAL,
#endif
    };

    esp_err_t err = uart_driver_install((uart_port_t)s_cfg.uart_num, 2048, 1024, 0, NULL, 0);
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        LOG_ERROR(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_param_config((uart_port_t)s_cfg.uart_num, &uart_cfg);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        (void)uart_driver_delete((uart_port_t)s_cfg.uart_num);
        return err;
    }

    err = uart_set_pin((uart_port_t)s_cfg.uart_num,
                       s_cfg.tx_pin,
                       s_cfg.rx_pin,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        (void)uart_driver_delete((uart_port_t)s_cfg.uart_num);
        return err;
    }

    s_ready = true;
    LOG_INFO(TAG,
             "HAL ready uart=%d tx=%d rx=%d baud=%lu",
             s_cfg.uart_num,
             s_cfg.tx_pin,
             s_cfg.rx_pin,
             (unsigned long)s_cfg.baudrate);
    return ESP_OK;
}

esp_err_t sim7080g_hal_deinit(void)
{
    if (!s_ready) {
        return ESP_OK;
    }

    (void)uart_driver_delete((uart_port_t)s_cfg.uart_num);
    if (s_at_mutex != NULL) {
        vSemaphoreDelete(s_at_mutex);
        s_at_mutex = NULL;
    }
    (void)memset(&s_cfg, 0, sizeof(s_cfg));
    s_ready = false;
    return ESP_OK;
}

esp_err_t sim7080g_hal_send_cmd(const char *cmd,
                                const char *expected,
                                char *response,
                                size_t response_len,
                                uint32_t timeout_ms,
                                uint8_t retries)
{
    if (!s_ready || (cmd == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (retries == 0U) {
        retries = 1U;
    }

    if (xSemaphoreTake(s_at_mutex, pdMS_TO_TICKS(timeout_ms + 500U)) != pdTRUE) {
        LOG_WARN(TAG, "mutex timeout for command");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t last_err = ESP_FAIL;
    for (uint8_t attempt = 0U; attempt < retries; ++attempt) {
        (void)uart_flush_input((uart_port_t)s_cfg.uart_num);
        if ((response != NULL) && (response_len > 0U)) {
            response[0] = '\0';
        }

        LOG_DEBUG(TAG, "> %s", cmd);
        int n = uart_write_bytes((uart_port_t)s_cfg.uart_num, cmd, (int)strlen(cmd));
        if (n < 0) {
            last_err = ESP_FAIL;
            continue;
        }
        n = uart_write_bytes((uart_port_t)s_cfg.uart_num, "\r\n", 2);
        if (n < 0) {
            last_err = ESP_FAIL;
            continue;
        }

        last_err = sim7080g_hal_wait_response_locked(expected, response, response_len, timeout_ms);
        if (last_err == ESP_OK) {
            break;
        }

        if ((response != NULL) && (response_len > 0U) && (response[0] != '\0')) {
            LOG_WARN(TAG,
                     "AT retry %u/%u cmd=%s err=%s resp=%s",
                     (unsigned)(attempt + 1U),
                     (unsigned)retries,
                     cmd,
                     esp_err_to_name(last_err),
                     response);
        } else {
            LOG_WARN(TAG,
                     "AT retry %u/%u cmd=%s err=%s",
                     (unsigned)(attempt + 1U),
                     (unsigned)retries,
                     cmd,
                     esp_err_to_name(last_err));
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    (void)xSemaphoreGive(s_at_mutex);
    return last_err;
}

esp_err_t sim7080g_hal_send_payload(const char *payload,
                                    bool append_crlf,
                                    const char *expected,
                                    uint32_t timeout_ms)
{
    if (!s_ready || (payload == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_at_mutex, pdMS_TO_TICKS(timeout_ms + 500U)) != pdTRUE) {
        LOG_WARN(TAG, "mutex timeout for payload");
        return ESP_ERR_TIMEOUT;
    }

    int n = uart_write_bytes((uart_port_t)s_cfg.uart_num, payload, (int)strlen(payload));
    if (n < 0) {
        (void)xSemaphoreGive(s_at_mutex);
        return ESP_FAIL;
    }
    if (append_crlf) {
        n = uart_write_bytes((uart_port_t)s_cfg.uart_num, "\r\n", 2);
        if (n < 0) {
            (void)xSemaphoreGive(s_at_mutex);
            return ESP_FAIL;
        }
    }

    const esp_err_t err =
        sim7080g_hal_wait_response_locked(expected, NULL, 0U, timeout_ms);
    (void)xSemaphoreGive(s_at_mutex);
    return err;
}

