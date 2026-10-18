#include "hardware_test.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "ds3231.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "fault_handler.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger.h"
#include "meter_config.h"
#include "meter_data.h"
#include "oled_driver.h"
#include "sd_storage.h"
#include "storage_service.h"
#include "sim7080g_hal.h"
#include "sim7080g_init.h"
#include "system_contracts_test.h"
#include "system_health.h"
#include "task_time.h"
#include "time_service.h"

static const char *TAG = "hw_test";

#define HW_TEST_RETRY_MS              5000U
#define HW_TEST_SD_LINES              16U
#define HW_TEST_SD_FILE               "hwtest_sd.txt"
#define HW_TEST_SD_COMBO_FILE         "hwtest_combo.txt"
#define HW_TEST_SIM_STATUS_PERIOD_MS  15000U
#define HW_TEST_PWRKEY_PULSE_MS       3000U
#define HW_TEST_PWRKEY_OFF_WAIT_MS    10000U
#define HW_TEST_PWRKEY_BOOT_WAIT_MS   30000U
#define HW_TEST_SD_SERVICE_MIN_MS     1800000U

#ifndef METER_RTC_TEST_SET_EPOCH
#define METER_RTC_TEST_SET_EPOCH      0LL
#endif

#ifndef METER_STORAGE_TEST_EPOCH
#define METER_STORAGE_TEST_EPOCH      1767225600LL
#endif

static void hardware_test_delay(uint32_t delay_ms)
{
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

static void hardware_test_log_result(const char *device,
                                     bool pass,
                                     const char *detail)
{
    if (pass) {
        LOG_INFO(TAG, "HWTEST RESULT %s PASS - %s", device, detail);
    } else {
        LOG_ERROR(TAG, "HWTEST RESULT %s FAIL - %s", device, detail);
    }
}

static void hardware_test_run_oled(void)
{
    unsigned long counter = 0UL;

    for (;;) {
        esp_err_t err = oled_driver_init();
        if (err != ESP_OK) {
            hardware_test_log_result("OLED", false,
                                     "sin respuesta en I2C 0x3C; revisar 3V3/GND/SDA8/SCL9");
            hardware_test_delay(HW_TEST_RETRY_MS);
            continue;
        }

        (void)oled_driver_clear();
        (void)oled_driver_draw_text(0U, "HW TEST OLED");
        (void)oled_driver_draw_text(1U, "SSD1306 128X64");
        (void)oled_driver_draw_text(2U, "I2C ADDR 0X3C");
        (void)oled_driver_draw_text(3U, "SDA8 SCL9");
        (void)oled_driver_draw_text(5U, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        (void)oled_driver_draw_text(6U, "0123456789");
        (void)oled_driver_draw_text(7U, "RESULT PASS");
        err = oled_driver_flush();
        if (err != ESP_OK) {
            hardware_test_log_result("OLED", false, "fallo enviando framebuffer");
            hardware_test_delay(HW_TEST_RETRY_MS);
            continue;
        }

        hardware_test_log_result("OLED", true,
                                 "0x3C responde; verificar imagen completa y estable");

        for (;;) {
            char line[32];
            (void)snprintf(line, sizeof(line), "COUNTER %06lu", counter++);
            if ((oled_driver_draw_text(4U, line) != ESP_OK) ||
                (oled_driver_flush() != ESP_OK)) {
                hardware_test_log_result("OLED", false,
                                         "display perdido durante refresco");
                break;
            }
            hardware_test_delay(1000U);
        }
    }
}

static void hardware_test_run_rtc(void)
{
    for (;;) {
        esp_err_t err = ds3231_init();
        if (err != ESP_OK) {
            hardware_test_log_result("RTC", false,
                                     "sin respuesta en I2C 0x68; revisar bus y bateria");
            hardware_test_delay(HW_TEST_RETRY_MS);
            continue;
        }

        time_t first = 0;
        time_t second = 0;
        err = ds3231_get_epoch(&first);
        if (err == ESP_OK) {
            hardware_test_delay(2100U);
            err = ds3231_get_epoch(&second);
        }

        const time_t delta = second - first;
        if ((err != ESP_OK) || (delta < 1) || (delta > 4)) {
            hardware_test_log_result("RTC", false,
                                     "el reloj no avanza al ritmo esperado");
            hardware_test_delay(HW_TEST_RETRY_MS);
            continue;
        }

        hardware_test_log_result("RTC", true,
                                 "0x68 responde y el contador avanza");

        for (;;) {
            struct tm rtc_time = {0};
            float temperature_c = 0.0f;
            err = ds3231_get_time(&rtc_time);
            const esp_err_t temp_err = ds3231_get_temperature(&temperature_c);
            if ((err != ESP_OK) || (temp_err != ESP_OK)) {
                hardware_test_log_result("RTC", false,
                                         "lectura de hora o temperatura perdida");
                break;
            }

            LOG_INFO(TAG,
                     "RTC %04d-%02d-%02d %02d:%02d:%02d UTC temp=%.2f C",
                     rtc_time.tm_year + 1900,
                     rtc_time.tm_mon + 1,
                     rtc_time.tm_mday,
                     rtc_time.tm_hour,
                     rtc_time.tm_min,
                     rtc_time.tm_sec,
                     (double)temperature_c);
            hardware_test_delay(1000U);
        }
    }
}

static esp_err_t hardware_test_start_rtc_service(void)
{
    esp_err_t err = system_health_init();
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        return err;
    }
    err = task_time_start();
    if (err != ESP_OK) {
        return err;
    }

    /* El epoch de prueba solo se aplica cuando la hora respaldada es
     * invalida, para no ocultar la prueba de retencion al reiniciar. */
    hardware_test_delay(2500U);
    if (!system_time_is_valid() &&
        system_time_epoch_is_valid((int64_t)METER_RTC_TEST_SET_EPOCH)) {
        err = time_service_submit_sync(SYSTEM_TIME_SOURCE_REMOTE,
                                       (int64_t)METER_RTC_TEST_SET_EPOCH);
        if (err != ESP_OK) {
            return err;
        }
        hardware_test_delay(1500U);
    }
    return ESP_OK;
}

static void hardware_test_run_rtc_service(bool with_oled)
{
    if (with_oled && (oled_driver_init() != ESP_OK)) {
        hardware_test_log_result("OLED+RTC_SERVICE", false,
                                 "OLED sin respuesta en 0x3C");
    }
    const esp_err_t start_err = hardware_test_start_rtc_service();
    if (start_err != ESP_OK) {
        hardware_test_log_result("RTC_SERVICE", false,
                                 "no se pudo iniciar el servicio RTOS");
    }

    bool pass_reported = false;
    unsigned long counter = 0UL;
    for (;;) {
        time_service_snapshot_t rtc = {0};
        system_time_status_t time_status = {0};
        system_health_snapshot_t rtc_health = {0};
        char local[24] = {0};
        (void)system_health_get_snapshot(SYSTEM_MODULE_RTC, &rtc_health);
        const bool snapshot_ok = (time_service_get_snapshot(&rtc) == ESP_OK);
        const bool time_status_ok =
            (system_time_get_status(&time_status) == ESP_OK);
        const bool valid = snapshot_ok && time_status_ok &&
            system_time_is_valid() &&
            (time_service_format_bogota(local, sizeof(local), true) == ESP_OK);

        if (valid && rtc.rtc_ready && rtc.rtc_time_valid && !pass_reported) {
            hardware_test_log_result(with_oled ? "OLED+RTC_SERVICE" : "RTC_SERVICE",
                                     true,
                                     "RTC valido, tarea RTOS activa y hora Bogota disponible");
            pass_reported = true;
        }

        LOG_INFO(TAG,
                 "RTC_SERVICE state=%s err=%s ready=%u rtc_valid=%u osf=%u eosc=%u time_valid=%u source=%s local=%s temp=%.2f failures=%lu",
                 system_state_name(rtc_health.state),
                 esp_err_to_name(rtc.rtc_last_error),
                 (unsigned)rtc.rtc_ready,
                 (unsigned)rtc.rtc_time_valid,
                 (unsigned)rtc.rtc_oscillator_stopped,
                 (unsigned)rtc.rtc_eosc_set,
                 (unsigned)system_time_is_valid(),
                 time_service_source_name(time_status.source),
                 valid ? local : "--",
                 (double)rtc.rtc_temperature_c,
                 (unsigned long)rtc.rtc_consecutive_failures);

        if (with_oled) {
            char state_line[24];
            char source_line[24];
            char temp_line[24];
            char count_line[24];
            (void)snprintf(state_line, sizeof(state_line), "RTC:%s TIME:%s",
                           rtc.rtc_ready ? "OK" : "--",
                           valid ? "OK" : "--");
            (void)snprintf(source_line, sizeof(source_line), "SRC:%s",
                           time_service_source_name(time_status.source));
            (void)snprintf(temp_line, sizeof(temp_line), "TEMP %.2f C",
                           (double)rtc.rtc_temperature_c);
            (void)snprintf(count_line, sizeof(count_line), "%s %06lu",
                           valid ? "PASS" : "WAIT", counter);
            (void)oled_driver_clear();
            (void)oled_driver_draw_text(0U, "RTC TIME SERVICE");
            (void)oled_driver_draw_text(1U, "OLED 0X3C");
            (void)oled_driver_draw_text(2U, state_line);
            (void)oled_driver_draw_text(3U, source_line);
            (void)oled_driver_draw_text(4U, valid ? local : "-- SIN FECHA --");
            (void)oled_driver_draw_text(5U, temp_line);
            char oscillator_line[24];
            (void)snprintf(oscillator_line, sizeof(oscillator_line),
                           "OSF:%u EOSC:%u",
                           (unsigned)rtc.rtc_oscillator_stopped,
                           (unsigned)rtc.rtc_eosc_set);
            (void)oled_driver_draw_text(6U, oscillator_line);
            (void)oled_driver_draw_text(7U, count_line);
            if (oled_driver_flush() != ESP_OK) {
                hardware_test_log_result("OLED+RTC_SERVICE", false,
                                         "fallo de bus I2C compartido");
            }
        }
        ++counter;
        hardware_test_delay(1000U);
    }
}

static void hardware_test_run_oled_rtc(void)
{
    for (;;) {
        esp_err_t err = oled_driver_init();
        if (err != ESP_OK) {
            hardware_test_log_result("OLED+RTC", false,
                                     "OLED sin respuesta en 0x3C");
            hardware_test_delay(HW_TEST_RETRY_MS);
            continue;
        }

        err = ds3231_init();
        if (err != ESP_OK) {
            hardware_test_log_result("OLED+RTC", false,
                                     "RTC sin respuesta en 0x68");
            hardware_test_delay(HW_TEST_RETRY_MS);
            continue;
        }

        time_t first = 0;
        time_t second = 0;
        err = ds3231_get_epoch(&first);
        if (err == ESP_OK) {
            hardware_test_delay(2100U);
            err = ds3231_get_epoch(&second);
        }
        const time_t delta = second - first;
        if ((err != ESP_OK) || (delta < 1) || (delta > 4)) {
            hardware_test_log_result("OLED+RTC", false,
                                     "RTC responde pero no avanza correctamente");
            hardware_test_delay(HW_TEST_RETRY_MS);
            continue;
        }

        (void)oled_driver_clear();
        (void)oled_driver_draw_text(0U, "HW TEST I2C");
        (void)oled_driver_draw_text(1U, "OLED + RTC");
        (void)oled_driver_draw_text(2U, "OLED 0X3C PASS");
        (void)oled_driver_draw_text(3U, "RTC  0X68 PASS");
        hardware_test_log_result("OLED+RTC", true,
                                 "bus compartido estable; ambos dispositivos responden");

        unsigned long counter = 0UL;
        for (;;) {
            struct tm rtc_time = {0};
            float temperature_c = 0.0f;
            err = ds3231_get_time(&rtc_time);
            const esp_err_t temp_err = ds3231_get_temperature(&temperature_c);
            if ((err != ESP_OK) || (temp_err != ESP_OK)) {
                hardware_test_log_result("OLED+RTC", false,
                                         "lectura RTC perdida en bus compartido");
                break;
            }

            char date_line[32];
            char time_line[32];
            char temp_line[32];
            char status_line[32];
            (void)snprintf(date_line, sizeof(date_line), "%04d-%02d-%02d",
                           rtc_time.tm_year + 1900,
                           rtc_time.tm_mon + 1,
                           rtc_time.tm_mday);
            (void)snprintf(time_line, sizeof(time_line), "%02d:%02d:%02d UTC",
                           rtc_time.tm_hour,
                           rtc_time.tm_min,
                           rtc_time.tm_sec);
            (void)snprintf(temp_line, sizeof(temp_line), "TEMP %.2f C",
                           (double)temperature_c);
            (void)snprintf(status_line, sizeof(status_line), "PASS %06lu", counter++);

            if ((oled_driver_draw_text(4U, date_line) != ESP_OK) ||
                (oled_driver_draw_text(5U, time_line) != ESP_OK) ||
                (oled_driver_draw_text(6U, temp_line) != ESP_OK) ||
                (oled_driver_draw_text(7U, status_line) != ESP_OK) ||
                (oled_driver_flush() != ESP_OK)) {
                hardware_test_log_result("OLED+RTC", false,
                                         "OLED perdida durante refresco conjunto");
                break;
            }

            LOG_INFO(TAG,
                     "OLED+RTC PASS count=%lu rtc=%04d-%02d-%02dT%02d:%02d:%02d temp=%.2fC",
                     counter - 1UL,
                     rtc_time.tm_year + 1900,
                     rtc_time.tm_mon + 1,
                     rtc_time.tm_mday,
                     rtc_time.tm_hour,
                     rtc_time.tm_min,
                     rtc_time.tm_sec,
                     (double)temperature_c);
            hardware_test_delay(1000U);
        }
    }
}

static esp_err_t hardware_test_sd_write_read(void)
{
    const long long session = (long long)esp_timer_get_time();

    for (unsigned i = 0U; i < HW_TEST_SD_LINES; ++i) {
        char line[96];
        (void)snprintf(line, sizeof(line),
                       "HWTEST session=%lld seq=%02u payload=SD_SPI_OK",
                       session, i);
        const esp_err_t err = sd_storage_append_line(HW_TEST_SD_FILE, line);
        if (err != ESP_OK) {
            return err;
        }
    }

    char fullpath[128];
    (void)snprintf(fullpath, sizeof(fullpath), "%s/%s",
                   SD_MOUNT_POINT, HW_TEST_SD_FILE);
    FILE *file = fopen(fullpath, "r");
    if (file == NULL) {
        return ESP_FAIL;
    }

    bool found[HW_TEST_SD_LINES] = {false};
    char actual[128];
    while (fgets(actual, sizeof(actual), file) != NULL) {
        const size_t len = strlen(actual);
        if ((len > 0U) && (actual[len - 1U] == '\n')) {
            actual[len - 1U] = '\0';
        }

        for (unsigned i = 0U; i < HW_TEST_SD_LINES; ++i) {
            char expected[96];
            (void)snprintf(expected, sizeof(expected),
                           "HWTEST session=%lld seq=%02u payload=SD_SPI_OK",
                           session, i);
            if (strcmp(actual, expected) == 0) {
                found[i] = true;
                break;
            }
        }
    }

    if (fclose(file) != 0) {
        return ESP_FAIL;
    }

    for (unsigned i = 0U; i < HW_TEST_SD_LINES; ++i) {
        if (!found[i]) {
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    return ESP_OK;
}

static void hardware_test_run_sd(void)
{
    for (;;) {
        esp_err_t err = sd_storage_init();
        if (err != ESP_OK) {
            hardware_test_log_result("SD", false,
                                     "no monta; revisar MOSI11/MISO13/SCLK12/CS7, VCC y FAT32");
            hardware_test_delay(HW_TEST_RETRY_MS);
            continue;
        }

        uint32_t total_mb = 0U;
        uint32_t free_mb = 0U;
        err = sd_storage_get_usage(&total_mb, &free_mb);
        if (err != ESP_OK) {
            hardware_test_log_result("SD", false, "no se pudo leer capacidad");
            hardware_test_delay(HW_TEST_RETRY_MS);
            continue;
        }

        err = hardware_test_sd_write_read();
        if (err != ESP_OK) {
            hardware_test_log_result("SD", false,
                                     "fallo de escritura/relectura de 16 registros");
            hardware_test_delay(HW_TEST_RETRY_MS);
            continue;
        }

        char detail[96];
        (void)snprintf(detail, sizeof(detail),
                       "FAT montado; 16/16 registros verificados; libre=%lu/%lu MB",
                       (unsigned long)free_mb,
                       (unsigned long)total_mb);
        hardware_test_log_result("SD", true, detail);
        LOG_INFO(TAG, "archivo de evidencia: %s/%s",
                 SD_MOUNT_POINT, HW_TEST_SD_FILE);

        for (;;) {
            if (sd_storage_get_usage(&total_mb, &free_mb) != ESP_OK) {
                hardware_test_log_result("SD", false,
                                         "tarjeta perdida durante monitoreo");
                break;
            }
            LOG_INFO(TAG, "SD presente libre=%lu/%lu MB",
                     (unsigned long)free_mb, (unsigned long)total_mb);
            hardware_test_delay(HW_TEST_SIM_STATUS_PERIOD_MS);
        }
    }
}

static void hardware_test_run_oled_rtc_sd(void)
{
    for (;;) {
        esp_err_t err = oled_driver_init();
        if (err != ESP_OK) {
            hardware_test_log_result("OLED+RTC+SD", false,
                                     "OLED sin respuesta en 0x3C");
            hardware_test_delay(HW_TEST_RETRY_MS);
            continue;
        }

        err = ds3231_init();
        if (err != ESP_OK) {
            hardware_test_log_result("OLED+RTC+SD", false,
                                     "RTC sin respuesta en 0x68");
            hardware_test_delay(HW_TEST_RETRY_MS);
            continue;
        }

        err = sd_storage_init();
        if (err != ESP_OK) {
            hardware_test_log_result("OLED+RTC+SD", false,
                                     "SD no monta; revisar SPI y FAT32");
            hardware_test_delay(HW_TEST_RETRY_MS);
            continue;
        }

        uint32_t total_mb = 0U;
        uint32_t free_mb = 0U;
        err = sd_storage_get_usage(&total_mb, &free_mb);
        if ((err != ESP_OK) || (hardware_test_sd_write_read() != ESP_OK)) {
            hardware_test_log_result("OLED+RTC+SD", false,
                                     "fallo de capacidad o escritura/relectura SD");
            hardware_test_delay(HW_TEST_RETRY_MS);
            continue;
        }

        time_t first = 0;
        time_t second = 0;
        err = ds3231_get_epoch(&first);
        if (err == ESP_OK) {
            hardware_test_delay(2100U);
            err = ds3231_get_epoch(&second);
        }
        const time_t delta = second - first;
        if ((err != ESP_OK) || (delta < 1) || (delta > 4)) {
            hardware_test_log_result("OLED+RTC+SD", false,
                                     "RTC no avanza durante actividad SD");
            hardware_test_delay(HW_TEST_RETRY_MS);
            continue;
        }

        (void)oled_driver_clear();
        (void)oled_driver_draw_text(0U, "HW TEST COMBO");
        (void)oled_driver_draw_text(1U, "OLED 0X3C PASS");
        (void)oled_driver_draw_text(2U, "RTC  0X68 PASS");
        (void)oled_driver_draw_text(3U, "SD FAT PASS");
        hardware_test_log_result("OLED+RTC+SD", true,
                                 "I2C y SPI activos; SD verificada 16/16");

        unsigned long counter = 0UL;
        unsigned long sd_writes = 0UL;
        for (;;) {
            struct tm rtc_time = {0};
            float temperature_c = 0.0f;
            err = ds3231_get_time(&rtc_time);
            const esp_err_t temp_err = ds3231_get_temperature(&temperature_c);
            if ((err != ESP_OK) || (temp_err != ESP_OK)) {
                hardware_test_log_result("OLED+RTC+SD", false,
                                         "lectura RTC perdida durante prueba SD");
                break;
            }

            if ((counter % 15UL) == 0UL) {
                char record[128];
                (void)snprintf(record, sizeof(record),
                               "COMBO count=%lu rtc=%04d-%02d-%02dT%02d:%02d:%02d temp=%.2fC",
                               counter,
                               rtc_time.tm_year + 1900,
                               rtc_time.tm_mon + 1,
                               rtc_time.tm_mday,
                               rtc_time.tm_hour,
                               rtc_time.tm_min,
                               rtc_time.tm_sec,
                               (double)temperature_c);
                if ((sd_storage_append_line(HW_TEST_SD_COMBO_FILE, record) != ESP_OK) ||
                    (sd_storage_get_usage(&total_mb, &free_mb) != ESP_OK)) {
                    hardware_test_log_result("OLED+RTC+SD", false,
                                             "SD perdida durante escritura periodica");
                    break;
                }
                ++sd_writes;
            }

            char time_line[32];
            char temp_line[32];
            char sd_line[32];
            char status_line[32];
            (void)snprintf(time_line, sizeof(time_line), "%02d:%02d:%02d UTC",
                           rtc_time.tm_hour,
                           rtc_time.tm_min,
                           rtc_time.tm_sec);
            (void)snprintf(temp_line, sizeof(temp_line), "TEMP %.2f C",
                           (double)temperature_c);
            (void)snprintf(sd_line, sizeof(sd_line), "SD %lu/%lu MB",
                           (unsigned long)free_mb,
                           (unsigned long)total_mb);
            (void)snprintf(status_line, sizeof(status_line), "PASS %06lu W%04lu",
                           counter, sd_writes);

            if ((oled_driver_draw_text(4U, time_line) != ESP_OK) ||
                (oled_driver_draw_text(5U, temp_line) != ESP_OK) ||
                (oled_driver_draw_text(6U, sd_line) != ESP_OK) ||
                (oled_driver_draw_text(7U, status_line) != ESP_OK) ||
                (oled_driver_flush() != ESP_OK)) {
                hardware_test_log_result("OLED+RTC+SD", false,
                                         "OLED perdida durante prueba conjunta");
                break;
            }

            LOG_INFO(TAG,
                     "OLED+RTC+SD PASS count=%lu writes=%lu rtc=%04d-%02d-%02dT%02d:%02d:%02d temp=%.2fC sd=%lu/%luMB",
                     counter,
                     sd_writes,
                     rtc_time.tm_year + 1900,
                     rtc_time.tm_mon + 1,
                     rtc_time.tm_mday,
                     rtc_time.tm_hour,
                     rtc_time.tm_min,
                     rtc_time.tm_sec,
                     (double)temperature_c,
                     (unsigned long)free_mb,
                     (unsigned long)total_mb);
            ++counter;
            hardware_test_delay(1000U);
        }
    }
}

static void hardware_test_response_one_line(char *response)
{
    if (response == NULL) {
        return;
    }
    for (char *p = response; *p != '\0'; ++p) {
        if ((*p == '\r') || (*p == '\n')) {
            *p = ' ';
        }
    }
}

static esp_err_t hardware_test_sim_query(const char *label,
                                         const char *cmd,
                                         const char *expected)
{
    char response[256] = {0};
    /* Wait for the final OK so the report includes the complete result
       (for example "+CPIN: READY" and the numeric CSQ value). */
    esp_err_t err = sim7080g_hal_send_cmd(cmd, NULL,
                                         response, sizeof(response),
                                         METER_SIM7080G_AT_TIMEOUT_MS,
                                         1U);
    if ((err == ESP_OK) && (expected != NULL) &&
        (strstr(response, expected) == NULL)) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    hardware_test_response_one_line(response);
    if (err == ESP_OK) {
        LOG_INFO(TAG, "SIM %-7s PASS cmd=%s resp=%s", label, cmd, response);
    } else {
        LOG_ERROR(TAG, "SIM %-7s FAIL cmd=%s err=%s resp=%s",
                  label, cmd, esp_err_to_name(err), response);
    }
    return err;
}

static esp_err_t hardware_test_sim_basic(void)
{
    esp_err_t err = sim7080g_init();
    if (err != ESP_OK) {
        return err;
    }

    if (hardware_test_sim_query("IDENT", "ATI", NULL) != ESP_OK) {
        return ESP_FAIL;
    }
    if (hardware_test_sim_query("SIM", "AT+CPIN?", "+CPIN: READY") != ESP_OK) {
        return ESP_FAIL;
    }
    if (hardware_test_sim_query("SIGNAL", "AT+CSQ", "+CSQ:") != ESP_OK) {
        return ESP_FAIL;
    }
    err = sim7080g_read_imei();
    if (err != ESP_OK) {
        return err;
    }
    LOG_INFO(TAG, "SIM IMEI PASS value=%s", sim7080g_get_imei());
    return ESP_OK;
}

static void hardware_test_run_sim_at(void)
{
    for (;;) {
        const esp_err_t err = hardware_test_sim_basic();
        if (err == ESP_OK) {
            hardware_test_log_result("SIM_AT", true,
                                     "UART, AT, SIM, CSQ e IMEI respondieron");
        } else {
            hardware_test_log_result("SIM_AT", false,
                                     "revisar alimentacion, PWRKEY, GND y cruce TX16/RX17");
        }
        hardware_test_delay(HW_TEST_SIM_STATUS_PERIOD_MS);
    }
}

static esp_err_t hardware_test_pwrkey_init(void)
{
    /* The Waveshare HAT exposes PWR through a 4.7 kOhm base resistor and an
       NPN transistor, not the modem's raw 1.8 V PWRKEY pin. PWR is therefore
       active-high: LOW releases the button and HIGH presses it. */
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
    return err;
}

static esp_err_t hardware_test_pwrkey_pulse(void)
{
    LOG_INFO(TAG, "PWR del HAT GPIO%d HIGH durante %u ms",
             SIM7080G_PIN_PWRKEY, (unsigned)HW_TEST_PWRKEY_PULSE_MS);
    esp_err_t err = gpio_set_level((gpio_num_t)SIM7080G_PIN_PWRKEY, 1);
    if (err != ESP_OK) {
        return err;
    }
    hardware_test_delay(HW_TEST_PWRKEY_PULSE_MS);
    err = gpio_set_level((gpio_num_t)SIM7080G_PIN_PWRKEY, 0);
    LOG_INFO(TAG, "PWR del HAT GPIO%d liberado en LOW",
             SIM7080G_PIN_PWRKEY);
    return err;
}

static void hardware_test_run_sim_pwrkey(void)
{
    esp_err_t err = hardware_test_pwrkey_init();
    if (err != ESP_OK) {
        hardware_test_log_result("SIM_PWRKEY", false,
                                 "no se pudo configurar GPIO PWR como salida");
        goto hold;
    }

    char response[64] = {0};
    err = hardware_test_sim_basic();
    if (err != ESP_OK) {
        LOG_WARN(TAG, "SIM_PWRKEY: modem inicialmente apagado; se probara solo encendido");
        sim7080g_mark_modem_unready();
        const sim7080g_hal_config_t uart_cfg = {
            .uart_num = SIM7080G_UART_NUM,
            .tx_pin = SIM7080G_UART_TX,
            .rx_pin = SIM7080G_UART_RX,
            .baudrate = SIM7080G_BAUD_RATE,
        };
        err = sim7080g_hal_init(&uart_cfg);
        if (err != ESP_OK) {
            hardware_test_log_result("SIM_PWRKEY", false,
                                     "no se pudo preparar UART para prueba de encendido");
            goto hold;
        }
        goto power_on;
    }
    hardware_test_log_result("SIM_PWRKEY PRECHECK", true,
                             "modem inicialmente encendido y UART activa");

    /* Exercise the carrier's active-high PWR input for both transitions. */
    err = hardware_test_pwrkey_pulse();
    if (err != ESP_OK) {
        hardware_test_log_result("SIM_PWRKEY OFF", false,
                                 "fallo generando pulso de apagado");
        goto hold;
    }
    sim7080g_mark_modem_unready();
    hardware_test_delay(HW_TEST_PWRKEY_OFF_WAIT_MS);

    response[0] = '\0';
    err = sim7080g_hal_send_cmd("AT", NULL, response, sizeof(response),
                                1000U, 1U);
    if (err == ESP_OK) {
        hardware_test_log_result("SIM_PWRKEY OFF", false,
                                 "el modem siguio respondiendo despues del pulso");
        goto hold;
    }
    hardware_test_log_result("SIM_PWRKEY OFF", true,
                             "GPIO apago el modem: UART dejo de responder");

power_on:
    err = hardware_test_pwrkey_pulse();
    if (err != ESP_OK) {
        hardware_test_log_result("SIM_PWRKEY", false,
                                 "fallo generando pulso de encendido");
        goto hold;
    }

    const TickType_t boot_start = xTaskGetTickCount();
    const TickType_t boot_timeout = pdMS_TO_TICKS(HW_TEST_PWRKEY_BOOT_WAIT_MS);
    do {
        hardware_test_delay(1000U);
        response[0] = '\0';
        err = sim7080g_hal_send_cmd("AT", NULL, response, sizeof(response),
                                    1000U, 1U);
        if (err == ESP_OK) {
            break;
        }
    } while ((xTaskGetTickCount() - boot_start) < boot_timeout);

    if (err != ESP_OK) {
        hardware_test_log_result("SIM_PWRKEY ON", false,
                                 "no reaparecio UART en 30 s; usar boton manual");
        goto hold;
    }

    sim7080g_mark_modem_unready();
    err = hardware_test_sim_basic();
    if (err != ESP_OK) {
        hardware_test_log_result("SIM_PWRKEY ON", false,
                                 "UART regreso pero fallo la inicializacion AT/SIM");
        goto hold;
    }

    hardware_test_log_result("SIM_PWRKEY", true,
                             "GPIO4 apago y encendio el modem; AT y SIM recuperados");
    for (;;) {
        hardware_test_delay(HW_TEST_SIM_STATUS_PERIOD_MS);
        (void)hardware_test_sim_query("AT", "AT", NULL);
    }

hold:
    (void)gpio_set_level((gpio_num_t)SIM7080G_PIN_PWRKEY, 0);
    for (;;) {
        hardware_test_delay(HW_TEST_RETRY_MS);
    }
}

static void hardware_test_run_sim_network(void)
{
    for (;;) {
        esp_err_t err = hardware_test_sim_basic();
        if (err == ESP_OK) {
            err = sim7080g_wait_network(METER_SIM7080G_NETWORK_TIMEOUT_MS);
        }
        if (err == ESP_OK) {
            err = sim7080g_activate_pdp();
        }
        if (err != ESP_OK) {
            hardware_test_log_result("SIM_NETWORK", false,
                                     "sin registro o PDP; revisar antena, SIM, APN y cobertura");
            hardware_test_delay(HW_TEST_SIM_STATUS_PERIOD_MS);
            continue;
        }

        (void)hardware_test_sim_query("REG", "AT+CEREG?", "+CEREG:");
        (void)hardware_test_sim_query("OPERATOR", "AT+COPS?", "+COPS:");
        (void)hardware_test_sim_query("PDP", "AT+CNACT?", "+CNACT:");
        hardware_test_log_result("SIM_NETWORK", true,
                                 "registro celular y contexto PDP activos");

        for (;;) {
            hardware_test_delay(HW_TEST_SIM_STATUS_PERIOD_MS);
            err = sim7080g_refresh_runtime_state();
            if (err != ESP_OK) {
                hardware_test_log_result("SIM_NETWORK", false,
                                         "se perdio red o PDP durante monitoreo");
                break;
            }
            (void)hardware_test_sim_query("SIGNAL", "AT+CSQ", "+CSQ:");
            hardware_test_log_result("SIM_NETWORK", true,
                                     "red y PDP siguen activos");
        }
    }
}

typedef struct {
    storage_record_kind_t kind;
    unsigned producer_id;
    TaskHandle_t notify_task;
} hardware_test_storage_producer_t;

static StaticTask_t s_storage_producer_tcb[2];
#define HW_TEST_STORAGE_PRODUCER_STACK 4096U
static StackType_t s_storage_producer_stack[2][HW_TEST_STORAGE_PRODUCER_STACK];
static hardware_test_storage_producer_t s_storage_producer_args[2];

static void hardware_test_storage_producer(void *arg)
{
    hardware_test_storage_producer_t *producer =
        (hardware_test_storage_producer_t *)arg;
    for (unsigned i = 0U; i < 12U; ++i) {
        static char payload[2][128];
        char *const producer_payload = payload[producer->producer_id];
        (void)snprintf(producer_payload, sizeof(payload[0]),
                       "[{\"n\":\"producer_%u\",\"v\":%u}]",
                       producer->producer_id, i);
        if (producer->kind == STORAGE_RECORD_ALERT) {
            (void)storage_service_submit_alert(producer_payload);
        } else {
            (void)storage_service_submit_telemetry(producer_payload);
        }
        vTaskDelay(pdMS_TO_TICKS(5U));
    }
    xTaskNotifyGive(producer->notify_task);
    vTaskDelete(NULL);
}

static void hardware_test_run_sd_service(void)
{
    bool recovery_ok = false;
    if (sd_storage_init() == ESP_OK) {
        (void)sd_storage_ensure_dir(METER_SD_BASE_DIR);
        const char recovery_file[] = METER_SD_BASE_DIR "/recovery_test.jsonl";
        char recovery_tmp[96];
        (void)snprintf(recovery_tmp, sizeof(recovery_tmp), "%s.tmp", recovery_file);
        (void)sd_storage_remove_file(recovery_file);
        (void)sd_storage_remove_file(recovery_tmp);
        const char recovery_line[] =
            "{\"v\":1,\"type\":\"recovery_test\",\"payload\":[{\"v\":1}]}";
        if ((sd_storage_append_line(recovery_tmp, recovery_line) == ESP_OK) &&
            (sd_storage_recover_file(recovery_file) == ESP_OK)) {
            static char readback[128];
            recovery_ok = (sd_storage_peek_line(recovery_file, readback,
                                                 sizeof(readback)) == ESP_OK) &&
                          (strcmp(readback, recovery_line) == 0);
        }
        (void)sd_storage_deinit();
    }
    hardware_test_log_result("SD_SERVICE TMP", recovery_ok,
                             "reemplazo .tmp recuperable en directorio de prueba");

    if ((fault_handler_init() != ESP_OK) || (meter_data_init() != ESP_OK) ||
        (storage_service_start() != ESP_OK)) {
        hardware_test_log_result("SD_SERVICE", false,
                                 "no se pudo iniciar contratos/datos/servicio");
        for (;;) {
            hardware_test_delay(HW_TEST_RETRY_MS);
        }
    }

    storage_service_snapshot_t snapshot = {0};
    const int64_t mount_deadline = esp_timer_get_time() + 15000000LL;
    do {
        hardware_test_delay(250U);
        (void)storage_service_get_snapshot(&snapshot);
    } while (!snapshot.mounted && (esp_timer_get_time() < mount_deadline));
    hardware_test_log_result("SD_SERVICE MOUNT", snapshot.mounted,
                             snapshot.mounted ? "montaje y capacidad disponibles"
                                              : "SD no montada en 15 s");

    const char invalid_payload[] = "[{\"n\":\"time_invalid\",\"v\":1}]";
    const uint32_t writes_before_invalid = snapshot.writes_ok;
    const bool invalid_queued =
        (storage_service_submit_telemetry(invalid_payload) == ESP_OK);
    const int64_t invalid_deadline = esp_timer_get_time() + 3000000LL;
    do {
        hardware_test_delay(50U);
        (void)storage_service_get_snapshot(&snapshot);
    } while ((snapshot.writes_ok <= writes_before_invalid) &&
             (esp_timer_get_time() < invalid_deadline));
    hardware_test_log_result("SD_SERVICE INVALID_TIME", invalid_queued,
                             "registro aceptado con timestamp invalido");

    const struct timeval injected_time = {
        .tv_sec = (time_t)METER_STORAGE_TEST_EPOCH,
        .tv_usec = 0,
    };
    const bool valid_time =
        (settimeofday(&injected_time, NULL) == 0) &&
        (system_time_mark_valid(SYSTEM_TIME_SOURCE_REMOTE,
                                (int64_t)METER_STORAGE_TEST_EPOCH) == ESP_OK);
    const char valid_payload[] = "[{\"n\":\"time_valid\",\"v\":1}]";
    const bool valid_queued = valid_time &&
        (storage_service_submit_telemetry(valid_payload) == ESP_OK);
    hardware_test_log_result("SD_SERVICE VALID_TIME", valid_queued,
                             "hora valida inyectada solo en reloj del sistema");

    hardware_test_delay(500U);
    static char pending_payload[STORAGE_SERVICE_PAYLOAD_MAX];
    const bool peek_ok =
        (storage_service_peek_pending(STORAGE_RECORD_TELEMETRY,
                                      pending_payload,
                                      sizeof(pending_payload)) == ESP_OK);
    const bool ack_ok = peek_ok &&
        (storage_service_ack_pending(STORAGE_RECORD_TELEMETRY) == ESP_OK);
    hardware_test_log_result("SD_SERVICE PEEK_ACK", ack_ok,
                             "lectura RAM y confirmacion asincrona");

    TaskHandle_t parent = xTaskGetCurrentTaskHandle();
    for (unsigned i = 0U; i < 2U; ++i) {
        s_storage_producer_args[i].kind = (i == 0U)
            ? STORAGE_RECORD_TELEMETRY : STORAGE_RECORD_ALERT;
        s_storage_producer_args[i].producer_id = i;
        s_storage_producer_args[i].notify_task = parent;
        (void)xTaskCreateStaticPinnedToCore(
            hardware_test_storage_producer, "sd_test_prod",
            HW_TEST_STORAGE_PRODUCER_STACK,
            &s_storage_producer_args[i], 2, s_storage_producer_stack[i],
            &s_storage_producer_tcb[i], tskNO_AFFINITY);
    }
    const bool producers_done =
        (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000U)) > 0U) &&
        (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000U)) > 0U);
    hardware_test_log_result("SD_SERVICE PRODUCERS", producers_done,
                             "dos productores concurrentes finalizaron");

    unsigned saturation_rejections = 0U;
    for (unsigned i = 0U; i < 32U; ++i) {
        if (storage_service_submit_telemetry(valid_payload) != ESP_OK) {
            ++saturation_rejections;
        }
    }
    hardware_test_log_result("SD_SERVICE QUEUE_FULL",
                             saturation_rejections > 0U,
                             "saturacion rechaza sin bloquear al productor");

#if defined(STORAGE_SERVICE_TEST_HOOKS) && (STORAGE_SERVICE_TEST_HOOKS != 0)
    storage_service_test_inject_usage(1000U, 200U);
    hardware_test_delay(METER_SD_USAGE_REFRESH_MS + 500U);
    (void)storage_service_get_snapshot(&snapshot);
    const bool warning_ok = snapshot.used_percent >= SYSTEM_SD_SPACE_WARNING_PCT;
    storage_service_test_inject_usage(1000U, 100U);
    hardware_test_delay((2U * METER_SD_USAGE_REFRESH_MS) + 1000U);
    (void)storage_service_get_snapshot(&snapshot);
    const bool critical_ok = snapshot.used_percent >= SYSTEM_SD_SPACE_CRITICAL_PCT;
    const bool purge_ok = snapshot.data_loss_records > 0U;
    storage_service_test_clear_usage();
    hardware_test_log_result("SD_SERVICE SPACE",
                             warning_ok && critical_ok && purge_ok,
                             "umbrales y purga inyectados sin llenar la tarjeta");
#endif

    const int64_t endurance_start = esp_timer_get_time();
    bool endurance_ok = true;
    while ((esp_timer_get_time() - endurance_start) <
           ((int64_t)HW_TEST_SD_SERVICE_MIN_MS * 1000LL)) {
        hardware_test_delay(10000U);
        if (storage_service_get_snapshot(&snapshot) != ESP_OK) {
            endurance_ok = false;
        }
        LOG_INFO(TAG,
                 "SD_SERVICE mounted=%u used=%u%% ok=%lu fail=%lu queued=%lu drop=%lu telem=%lu alerts=%lu main_stack_min=%u",
                 (unsigned)snapshot.mounted, (unsigned)snapshot.used_percent,
                 (unsigned long)snapshot.writes_ok,
                 (unsigned long)snapshot.writes_failed,
                 (unsigned long)snapshot.requests_pending,
                 (unsigned long)snapshot.requests_dropped,
                 (unsigned long)snapshot.telemetry_pending,
                 (unsigned long)snapshot.alerts_pending,
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
    hardware_test_log_result("SD_SERVICE 30MIN", endurance_ok,
                             "servicio continuo sin reinicio");
    for (;;) {
        hardware_test_delay(HW_TEST_RETRY_MS);
    }
}

bool hardware_test_is_standalone(void)
{
    return ((METER_HW_TEST_PROFILE >= HW_TEST_PROFILE_OLED) &&
            (METER_HW_TEST_PROFILE <= HW_TEST_PROFILE_SIM_NETWORK)) ||
           (METER_HW_TEST_PROFILE == HW_TEST_PROFILE_SIM_PWRKEY) ||
           (METER_HW_TEST_PROFILE == HW_TEST_PROFILE_OLED_RTC) ||
           (METER_HW_TEST_PROFILE == HW_TEST_PROFILE_OLED_RTC_SD) ||
           (METER_HW_TEST_PROFILE == HW_TEST_PROFILE_SYSTEM_CONTRACTS) ||
           (METER_HW_TEST_PROFILE == HW_TEST_PROFILE_RTC_SERVICE) ||
           (METER_HW_TEST_PROFILE == HW_TEST_PROFILE_OLED_RTC_SERVICE) ||
           (METER_HW_TEST_PROFILE == HW_TEST_PROFILE_SD_SERVICE);
}

const char *hardware_test_profile_name(void)
{
    switch (METER_HW_TEST_PROFILE) {
        case HW_TEST_PROFILE_NONE:        return "normal";
        case HW_TEST_PROFILE_OLED:        return "test_oled";
        case HW_TEST_PROFILE_RTC:         return "test_rtc";
        case HW_TEST_PROFILE_SD:          return "test_sd";
        case HW_TEST_PROFILE_SIM_AT:      return "test_sim_at";
        case HW_TEST_PROFILE_SIM_NETWORK: return "test_sim_network";
        case HW_TEST_PROFILE_ALL_NO_ADE:  return "test_all_no_ade";
        case HW_TEST_PROFILE_SIM_PWRKEY:  return "test_sim_pwrkey";
        case HW_TEST_PROFILE_OLED_RTC:    return "test_oled_rtc";
        case HW_TEST_PROFILE_OLED_RTC_SD: return "test_oled_rtc_sd";
        case HW_TEST_PROFILE_SYSTEM_CONTRACTS: return "test_system_contracts";
        case HW_TEST_PROFILE_RTC_SERVICE: return "test_rtc_service";
        case HW_TEST_PROFILE_OLED_RTC_SERVICE: return "test_oled_rtc_service";
        case HW_TEST_PROFILE_SD_SERVICE: return "test_sd_service";
        case HW_TEST_PROFILE_OLED_RTC_SD_SERVICE:
            return "test_oled_rtc_sd_service";
        default:                          return "invalid";
    }
}

void hardware_test_run(void)
{
    LOG_WARN(TAG, "============================================================");
    LOG_WARN(TAG, "HARDWARE TEST PROFILE: %s", hardware_test_profile_name());
    LOG_WARN(TAG, "ADE9153A EXCLUIDO - NO ENERGIZAR LA ZONA AC PARA ESTA PRUEBA");
    LOG_WARN(TAG, "============================================================");

    switch (METER_HW_TEST_PROFILE) {
        case HW_TEST_PROFILE_OLED:
            hardware_test_run_oled();
            break;
        case HW_TEST_PROFILE_RTC:
            hardware_test_run_rtc();
            break;
        case HW_TEST_PROFILE_SD:
            hardware_test_run_sd();
            break;
        case HW_TEST_PROFILE_SIM_AT:
            hardware_test_run_sim_at();
            break;
        case HW_TEST_PROFILE_SIM_NETWORK:
            hardware_test_run_sim_network();
            break;
        case HW_TEST_PROFILE_SIM_PWRKEY:
            hardware_test_run_sim_pwrkey();
            break;
        case HW_TEST_PROFILE_OLED_RTC:
            hardware_test_run_oled_rtc();
            break;
        case HW_TEST_PROFILE_OLED_RTC_SD:
            hardware_test_run_oled_rtc_sd();
            break;
        case HW_TEST_PROFILE_SYSTEM_CONTRACTS:
            LOG_INFO(TAG, "Esperando 5 s para conectar el monitor serial...");
            hardware_test_delay(5000U);
            if (system_contracts_test_run()) {
                hardware_test_log_result("SYSTEM_CONTRACTS", true,
                                         "states/dedup/time/overflow/concurrency");
            } else {
                hardware_test_log_result("SYSTEM_CONTRACTS", false,
                                         "revisar log de assertions");
            }
            for (;;) {
                hardware_test_delay(HW_TEST_RETRY_MS);
            }
            break;
        case HW_TEST_PROFILE_RTC_SERVICE:
            hardware_test_run_rtc_service(false);
            break;
        case HW_TEST_PROFILE_OLED_RTC_SERVICE:
            hardware_test_run_rtc_service(true);
            break;
        case HW_TEST_PROFILE_SD_SERVICE:
            hardware_test_run_sd_service();
            break;
        default:
            hardware_test_log_result("PROFILE", false,
                                     "perfil standalone invalido");
            for (;;) {
                hardware_test_delay(HW_TEST_RETRY_MS);
            }
    }
}
