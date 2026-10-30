#include "sim7080g_power.h"

#include <limits.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger.h"
#include "meter_config.h"
#include "sim7080g_hal.h"

static const char *TAG = "sim7080g_power";
static sim7080g_power_snapshot_t s_power;

esp_err_t sim7080g_power_init(void)
{
    if (s_power.initialized) {
        return ESP_OK;
    }

    esp_err_t err = gpio_set_level((gpio_num_t)SIM7080G_PIN_PWRKEY, 0);
    if (err != ESP_OK) {
        return err;
    }
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << SIM7080G_PIN_PWRKEY),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&config);
    if (err == ESP_OK) {
        err = gpio_set_level((gpio_num_t)SIM7080G_PIN_PWRKEY, 0);
    }
    if (err != ESP_OK) {
        return err;
    }

    s_power.initialized = true;
    LOG_INFO(TAG, "PWRKEY GPIO%d listo: LOW inactivo, HIGH activo",
             SIM7080G_PIN_PWRKEY);
    return ESP_OK;
}

esp_err_t sim7080g_power_handle_no_at(bool *pulsed)
{
    if (pulsed == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *pulsed = false;

    const esp_err_t init_err = sim7080g_power_init();
    if (init_err != ESP_OK) {
        return init_err;
    }

    if (s_power.consecutive_no_at < UINT8_MAX) {
        ++s_power.consecutive_no_at;
    }
#if !METER_SIM7080G_AUTO_PWRKEY_RECOVERY
    LOG_WARN(TAG,
             "UART sin AT (%u); PWRKEY automatico inhibido por perfil A/B",
             (unsigned)s_power.consecutive_no_at);
    return ESP_ERR_NOT_FINISHED;
#endif
    if (s_power.consecutive_no_at < METER_SIM7080G_PWRKEY_NO_AT_PROBES) {
        LOG_WARN(TAG, "UART sin AT (%u/%u), PWRKEY aun inhibido",
                 (unsigned)s_power.consecutive_no_at,
                 (unsigned)METER_SIM7080G_PWRKEY_NO_AT_PROBES);
        return ESP_ERR_NOT_FINISHED;
    }

    const int64_t now_us = esp_timer_get_time();
    // PWRKEY conmuta el modem: sobre uno ocupado (que respondio hace poco)
    // lo apagaria. Solo se pulsa tras un silencio UART prolongado.
    const int64_t last_rx_us = sim7080g_hal_last_rx_us();
    if ((last_rx_us > 0) &&
        ((now_us - last_rx_us) < ((int64_t)METER_SIM7080G_PWRKEY_MIN_SILENCE_MS * 1000LL))) {
        LOG_WARN(TAG, "PWRKEY inhibido: el modem respondio hace %lu ms",
                 (unsigned long)((now_us - last_rx_us) / 1000LL));
        return ESP_ERR_NOT_FINISHED;
    }
    const int64_t since_pulse_us = now_us - s_power.last_pulse_uptime_us;
    if ((s_power.last_pulse_uptime_us > 0) &&
        (since_pulse_us < ((int64_t)METER_SIM7080G_PWRKEY_BOOT_WAIT_MS * 1000LL))) {
        return ESP_ERR_NOT_FINISHED;
    }
    if (s_power.pulse_attempts >= METER_SIM7080G_PWRKEY_MAX_ATTEMPTS) {
        if ((s_power.last_pulse_uptime_us > 0) &&
            (since_pulse_us < ((int64_t)METER_SIM7080G_PWRKEY_SLOW_RETRY_MS * 1000LL))) {
            LOG_WARN(TAG, "PWRKEY en cooldown tras %u intentos",
                     (unsigned)s_power.pulse_attempts);
            return ESP_ERR_TIMEOUT;
        }
        s_power.pulse_attempts = 0U;
    }

    LOG_WARN(TAG, "PWRKEY GPIO%d HIGH durante %u ms (intento %u/%u)",
             SIM7080G_PIN_PWRKEY,
             (unsigned)METER_SIM7080G_PWRKEY_PULSE_MS,
             (unsigned)(s_power.pulse_attempts + 1U),
             (unsigned)METER_SIM7080G_PWRKEY_MAX_ATTEMPTS);
    esp_err_t err = gpio_set_level((gpio_num_t)SIM7080G_PIN_PWRKEY, 1);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(METER_SIM7080G_PWRKEY_PULSE_MS));
    err = gpio_set_level((gpio_num_t)SIM7080G_PIN_PWRKEY, 0);
    if (err != ESP_OK) {
        return err;
    }

    ++s_power.pulse_attempts;
    ++s_power.pulses_total;
    s_power.consecutive_no_at = 0U;
    s_power.last_pulse_uptime_us = esp_timer_get_time();
    *pulsed = true;

    LOG_INFO(TAG, "PWRKEY liberado en LOW; esperando %u ms de arranque",
             (unsigned)METER_SIM7080G_PWRKEY_BOOT_WAIT_MS);
    vTaskDelay(pdMS_TO_TICKS(METER_SIM7080G_PWRKEY_BOOT_WAIT_MS));
    return ESP_OK;
}

void sim7080g_power_note_at_ready(void)
{
    s_power.consecutive_no_at = 0U;
    s_power.pulse_attempts = 0U;
}

esp_err_t sim7080g_power_get_snapshot(sim7080g_power_snapshot_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = s_power;
    return ESP_OK;
}
