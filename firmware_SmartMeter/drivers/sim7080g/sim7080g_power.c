#include "sim7080g_power.h"

#include <limits.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_system.h"
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
    // rc4: tras un encendido en frio el HAT tambien estuvo sin energia y el
    // modem arranca apagado; tras un reinicio solo del ESP32 puede seguir
    // encendido (y ocupado), y un pulso lo apagaria.
    s_power.cold_boot = (esp_reset_reason() == ESP_RST_POWERON);
    LOG_INFO(TAG, "PWRKEY GPIO%d listo: LOW inactivo, HIGH activo (arranque %s)",
             SIM7080G_PIN_PWRKEY, s_power.cold_boot ? "en frio" : "en caliente");
    return ESP_OK;
}

static esp_err_t sim7080g_power_pulse(void)
{
    LOG_WARN(TAG, "PWRKEY GPIO%d HIGH durante %u ms",
             SIM7080G_PIN_PWRKEY, (unsigned)METER_SIM7080G_PWRKEY_PULSE_MS);
    esp_err_t err = gpio_set_level((gpio_num_t)SIM7080G_PIN_PWRKEY, 1);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(METER_SIM7080G_PWRKEY_PULSE_MS));
    err = gpio_set_level((gpio_num_t)SIM7080G_PIN_PWRKEY, 0);
    if (err != ESP_OK) {
        return err;
    }
    ++s_power.pulses_total;
    s_power.last_pulse_uptime_us = esp_timer_get_time();
    return ESP_OK;
}

static bool sim7080g_power_modem_answers(void)
{
    return sim7080g_hal_is_ready() &&
           (sim7080g_hal_send_cmd("AT", NULL, NULL, 0U, 1000U, 2U) == ESP_OK);
}

static bool sim7080g_power_wait_answer(uint32_t timeout_ms)
{
    const int64_t start_us = esp_timer_get_time();
    while ((esp_timer_get_time() - start_us) < ((int64_t)timeout_ms * 1000LL)) {
        vTaskDelay(pdMS_TO_TICKS(2000U));
        if (sim7080g_power_modem_answers()) {
            return true;
        }
    }
    return false;
}

esp_err_t sim7080g_power_hard_cycle(void)
{
    const esp_err_t init_err = sim7080g_power_init();
    if (init_err != ESP_OK) {
        return init_err;
    }
    LOG_WARN(TAG, "N3: reinicio fisico del modem (CPOWD + PWRKEY)");

    char response[96];
    response[0] = '\0';
    const esp_err_t pd_err = sim7080g_hal_send_cmd("AT+CPOWD=1", "NORMAL POWER DOWN",
                                                   response, sizeof(response),
                                                   10000U, 1U);
    LOG_INFO(TAG, "CPOWD=1: %s", (pd_err == ESP_OK) ? "NORMAL POWER DOWN" : "sin confirmacion");
    vTaskDelay(pdMS_TO_TICKS(5000U));

    if (sim7080g_power_modem_answers()) {
        // CPOWD no lo apago: el pulso conmuta un modem encendido a apagado.
        LOG_WARN(TAG, "N3: el modem sigue contestando; pulso para apagarlo");
        esp_err_t err = sim7080g_power_pulse();
        if (err != ESP_OK) {
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(6000U));
        if (sim7080g_power_modem_answers()) {
            LOG_ERROR(TAG, "N3 abortado: el modem no se apago");
            return ESP_ERR_INVALID_STATE;
        }
    }

    // Apagado confirmado: ahora un pulso solo puede encenderlo.
    vTaskDelay(pdMS_TO_TICKS(3000U));
    esp_err_t err = sim7080g_power_pulse();
    if (err != ESP_OK) {
        return err;
    }
    bool back = sim7080g_power_wait_answer(40000U);
    if (!back) {
        LOG_WARN(TAG, "N3: sin AT 40 s tras el pulso de encendido; segundo pulso");
        err = sim7080g_power_pulse();
        if (err != ESP_OK) {
            return err;
        }
        back = sim7080g_power_wait_answer(40000U);
    }
    ++s_power.hard_cycles_total;
    s_power.consecutive_no_at = 0U;
    s_power.pulse_attempts = 0U;
    LOG_INFO(TAG, "N3: %s", back ? "modem encendido y contestando AT" : "modem sin AT");
    return back ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t sim7080g_power_off_if_on(void)
{
    esp_err_t err = sim7080g_power_init();
    if (err != ESP_OK) {
        return err;
    }
    const sim7080g_hal_config_t cfg = {
        .uart_num = SIM7080G_UART_NUM,
        .tx_pin = SIM7080G_UART_TX,
        .rx_pin = SIM7080G_UART_RX,
        .baudrate = (uint32_t)SIM7080G_BAUD_RATE,
    };
    err = sim7080g_hal_init(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    if (!sim7080g_power_modem_answers()) {
        LOG_INFO(TAG, "modem sin AT: se considera apagado");
        return ESP_OK;
    }
    char response[96];
    response[0] = '\0';
    const esp_err_t pd_err = sim7080g_hal_send_cmd("AT+CPOWD=1", "NORMAL POWER DOWN",
                                                   response, sizeof(response),
                                                   10000U, 1U);
    LOG_WARN(TAG, "perfil sin datos: CPOWD=1 %s",
             (pd_err == ESP_OK) ? "NORMAL POWER DOWN" : "sin confirmacion");
    vTaskDelay(pdMS_TO_TICKS(5000U));
    if (sim7080g_power_modem_answers()) {
        LOG_WARN(TAG, "perfil sin datos: el modem sigue encendido; pulso PWRKEY");
        err = sim7080g_power_pulse();
        if (err != ESP_OK) {
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(6000U));
        if (sim7080g_power_modem_answers()) {
            LOG_ERROR(TAG, "perfil sin datos: no se pudo apagar el modem");
            return ESP_ERR_INVALID_STATE;
        }
    }
    LOG_WARN(TAG, "perfil sin datos: modem APAGADO (sin consumo de datos)");
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
    if ((last_rx_us <= 0) && !s_power.cold_boot &&
        (now_us < ((int64_t)METER_SIM7080G_PWRKEY_WARM_BOOT_SILENCE_MS * 1000LL))) {
        // Reinicio en caliente sin respuesta AT aun: el modem pudo quedar
        // encendido y ocupado con un SMCONN/SMPUB. Esperar antes de pulsar.
        LOG_WARN(TAG, "PWRKEY inhibido: arranque en caliente, %lu s sin AT de %u s",
                 (unsigned long)(now_us / 1000000LL),
                 (unsigned)(METER_SIM7080G_PWRKEY_WARM_BOOT_SILENCE_MS / 1000U));
        return ESP_ERR_NOT_FINISHED;
    }
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
