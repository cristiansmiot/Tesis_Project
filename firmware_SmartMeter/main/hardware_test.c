#include "hardware_test.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "active_power.h"
#include "ade9153a_init.h"
#include "ade9153a_regs.h"
#include "ade9153a_spi.h"
#include "apparent_power.h"
#include "board_config.h"
#include "conversion_constants.h"
#include "current.h"
#include "driver/gpio.h"
#include "ds3231.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "fault_handler.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "harmonics.h"
#include "logger.h"
#include "meter_config.h"
#include "meter_data.h"
#include "../modules/communication/mqtt_client.h"
#include "../modules/communication/mqtt_topics.h"
#include "msure.h"
#include "oled_driver.h"
#include "pq_monitor.h"
#include "power_factor.h"
#include "reactive_power.h"
#include "sag_swell.h"
#include "sd_storage.h"
#include "storage_service.h"
#include "sim7080g_hal.h"
#include "sim7080g_init.h"
#include "system_contracts_test.h"
#include "system_health.h"
#include "task_time.h"
#include "temperature.h"
#include "time_service.h"
#include "voltage.h"

static const char *TAG = "hw_test";

#define HW_TEST_RETRY_MS              5000U
#define HW_TEST_SD_LINES              16U
#define HW_TEST_SD_FILE               "hwtest_sd.txt"
#define HW_TEST_SD_COMBO_FILE         "hwtest_combo.txt"
#define HW_TEST_SIM_STATUS_PERIOD_MS  15000U
#define HW_TEST_SIM_MQTT_PUBLISH_PERIOD_MS 60000U
#define HW_TEST_SIM_TCP_PERIOD_MS     60000U
#define HW_TEST_SIM_TCP_OPEN_TIMEOUT_MS 45000U
#define HW_TEST_SIM_TCP_MAX_ATTEMPTS   3U
#define HW_TEST_PWRKEY_PULSE_MS       3000U
#define HW_TEST_PWRKEY_OFF_WAIT_MS    10000U
#define HW_TEST_PWRKEY_BOOT_WAIT_MS   30000U
#define HW_TEST_SD_SERVICE_MIN_MS     1800000U
#define HW_TEST_ADE_STABILITY_SAMPLES 30U
#define HW_TEST_ADE_MEASUREMENT_SAMPLES 30U

static const char *s_sim_at_stage = "BOOT";
static char s_sim_at_csq[16] = "--";

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

static void hardware_test_run_ade_smoke(void)
{
    LOG_WARN(TAG, "PRUEBA DIGITAL ADE9153A: mantener las entradas AC desconectadas");
    LOG_WARN(TAG, "Shield: alimentacion 5V y GND; jumper SPI 5V abierto, jumper SPI 3V cerrado");
    LOG_INFO(TAG,
             "ADE pins MOSI=%d MISO=%d SCLK=%d CS=%d RESET=%d IRQ=%d SPI=%dHz mode=%d",
             ADE9153A_PIN_MOSI, ADE9153A_PIN_MISO, ADE9153A_PIN_SCLK,
             ADE9153A_PIN_CS, ADE9153A_PIN_RESET, ADE9153A_PIN_IRQ,
             ADE9153A_SPI_CLK_HZ, ADE9153A_SPI_MODE);

    /* Mantener inactivos ambos perifericos SPI durante el arranque. */
    const gpio_config_t sd_cs_cfg = {
        .pin_bit_mask = (1ULL << SD_PIN_CS) | (1ULL << ADE9153A_PIN_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&sd_cs_cfg);
    if (err == ESP_OK) {
        err = gpio_set_level(SD_PIN_CS, 1);
    }
    if (err == ESP_OK) {
        err = gpio_set_level(ADE9153A_PIN_CS, 1);
    }
    if (err != ESP_OK) {
        hardware_test_log_result("ADE SD_CS", false,
                                 "no se pudo aislar la microSD del bus SPI");
        for (;;) {
            hardware_test_delay(HW_TEST_RETRY_MS);
        }
    }

    err = ade9153a_spi_init();
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "ADE SPI init error: %s", esp_err_to_name(err));
        hardware_test_log_result("ADE SPI", false,
                                 "revisar 5V/GND, jumper SPI 3V y MOSI11/MISO13/SCLK12/CS10/RST14");
        for (;;) {
            hardware_test_delay(HW_TEST_RETRY_MS);
        }
    }
    hardware_test_log_result("ADE SPI", true, "bus SPI2 inicializado a 1 MHz");

    err = ade9153a_init();
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "ADE init error: %s", esp_err_to_name(err));
        hardware_test_log_result("ADE INIT", false,
                                 "chip sin respuesta valida tras reset/configuracion");
        for (;;) {
            hardware_test_delay(HW_TEST_RETRY_MS);
            ade9153a_diag_t failed = {0};
            const esp_err_t diag_err = ade9153a_get_diag(&failed);
            LOG_ERROR(TAG,
                      "ADE init FAIL monitor RUN=0x%04x VERSION=0x%04x PRODUCT=0x%08lx err=%s",
                      (unsigned)failed.run, (unsigned)failed.version,
                      (unsigned long)failed.product, esp_err_to_name(diag_err));
        }
    }

    ade9153a_diag_t first = {0};
    err = ade9153a_get_diag(&first);
    LOG_INFO(TAG,
             "ADE first RUN=0x%04x VERSION=0x%04x PRODUCT=0x%08lx CHIP=%08lx:%08lx ready=%u bus_off=%u err=%s",
             (unsigned)first.run, (unsigned)first.version,
             (unsigned long)first.product, (unsigned long)first.chip_hi,
             (unsigned long)first.chip_lo, (unsigned)first.ready,
             (unsigned)first.bus_disconnected, esp_err_to_name(err));
    if ((err != ESP_OK) || !first.ready || first.bus_disconnected) {
        hardware_test_log_result("ADE ID", false,
                                 "RUN/identidad no validos; revisar alimentacion logica y SPI");
        for (;;) {
            hardware_test_delay(HW_TEST_RETRY_MS);
        }
    }
    hardware_test_log_result("ADE ID", true,
                             "RUN, VERSION, PRODUCT y CHIP_ID responden");

    bool stable = true;
    for (uint32_t i = 1U; i <= HW_TEST_ADE_STABILITY_SAMPLES; ++i) {
        ade9153a_diag_t sample = {0};
        hardware_test_delay(1000U);
        err = ade9153a_get_diag(&sample);
        const bool same_identity = (sample.version == first.version) &&
                                   (sample.product == first.product) &&
                                   (sample.chip_hi == first.chip_hi) &&
                                   (sample.chip_lo == first.chip_lo);
        const bool sample_ok = (err == ESP_OK) && sample.ready &&
                               !sample.bus_disconnected && same_identity;
        LOG_INFO(TAG,
                 "ADE sample=%02lu/%u ok=%u RUN=0x%04x VERSION=0x%04x PRODUCT=0x%08lx",
                 (unsigned long)i, (unsigned)HW_TEST_ADE_STABILITY_SAMPLES,
                 (unsigned)sample_ok, (unsigned)sample.run,
                 (unsigned)sample.version, (unsigned long)sample.product);
        if (!sample_ok) {
            stable = false;
            break;
        }
    }
    hardware_test_log_result("ADE SPI STABILITY", stable,
                             stable ? "30/30 lecturas coherentes"
                                    : "lectura perdida o identidad cambiante");

    float temp_c = 0.0f;
    const esp_err_t temp_init_err = temperature_init();
    const esp_err_t temp_err = (temp_init_err == ESP_OK)
                                   ? temperature_read_internal(&temp_c)
                                   : temp_init_err;
    if (temp_err == ESP_OK) {
        LOG_INFO(TAG, "ADE internal temperature=%.2f C", (double)temp_c);
    } else {
        LOG_ERROR(TAG, "ADE temperature error: %s", esp_err_to_name(temp_err));
    }
    hardware_test_log_result("ADE TEMPERATURE", temp_err == ESP_OK,
                             (temp_err == ESP_OK) ? "TEMP_TRIM y TEMP_RSLT validos"
                                                  : "temperatura interna no valida");

    const bool overall = stable && (temp_err == ESP_OK);
    hardware_test_log_result("ADE_SMOKE", overall,
                             overall ? "SPI, identidad, DSP y temperatura aprobados; sin AC"
                                     : "revisar el subresultado que fallo");

    for (;;) {
        ade9153a_diag_t live = {0};
        hardware_test_delay(HW_TEST_RETRY_MS);
        const esp_err_t live_err = ade9153a_get_diag(&live);
        LOG_INFO(TAG,
                 "ADE monitor ready=%u bus_off=%u RUN=0x%04x VERSION=0x%04x err=%s",
                 (unsigned)live.ready, (unsigned)live.bus_disconnected,
                 (unsigned)live.run, (unsigned)live.version,
                 esp_err_to_name(live_err));
    }
}

typedef struct {
    uint32_t airms;
    uint32_t avrms;
    uint32_t awatt;
    uint32_t ava;
    uint32_t afvar;
    uint32_t apf;
    uint32_t aperiod;
    uint32_t awatthr_hi;
    uint32_t awatthr_lo;
    float vrms_v;
    float irms_a;
    float active_w;
    float reactive_var;
    float apparent_va;
    float power_factor;
    float frequency_hz;
    float active_wh;
    float temperature_c;
    bool ac_valid;
} hardware_test_ade_sample_t;

static bool hardware_test_ade_check_reg16(const char *name,
                                          uint16_t address,
                                          uint16_t expected)
{
    uint16_t actual = 0U;
    const esp_err_t err = ade9153a_read_reg16(address, &actual);
    const bool pass = (err == ESP_OK) && (actual == expected);
    LOG_INFO(TAG,
             "ADE CFG %-12s addr=0x%04x actual=0x%04x expected=0x%04x %s",
             name, (unsigned)address, (unsigned)actual, (unsigned)expected,
             pass ? "PASS" : "FAIL");
    return pass;
}

static bool hardware_test_ade_check_reg32(const char *name,
                                          uint16_t address,
                                          uint32_t expected)
{
    uint32_t actual = 0U;
    const esp_err_t err = ade9153a_read_reg32(address, &actual);
    const bool pass = (err == ESP_OK) && (actual == expected);
    LOG_INFO(TAG,
             "ADE CFG %-12s addr=0x%04x actual=0x%08lx expected=0x%08lx %s",
             name, (unsigned)address, (unsigned long)actual,
             (unsigned long)expected, pass ? "PASS" : "FAIL");
    return pass;
}

static bool hardware_test_ade_validate_config(void)
{
    bool pass = true;

    pass = hardware_test_ade_check_reg16("RUN", ADE9153A_REG_RUN, 0x0001U) && pass;
    pass = hardware_test_ade_check_reg16("AI_PGAGAIN", ADE9153A_REG_AI_PGAGAIN, 0x000AU) && pass;
    pass = hardware_test_ade_check_reg16("BI_PGAGAIN", ADE9153A_REG_BI_PGAGAIN, 0x0000U) && pass;
    pass = hardware_test_ade_check_reg16("ACCMODE", ADE9153A_REG_ACCMODE, 0x0010U) && pass;
    pass = hardware_test_ade_check_reg16("CONFIG1", ADE9153A_REG_CONFIG1, 0x0300U) && pass;
    pass = hardware_test_ade_check_reg16("CONFIG2", ADE9153A_REG_CONFIG2, 0x0C00U) && pass;
    pass = hardware_test_ade_check_reg16("CONFIG3", ADE9153A_REG_CONFIG3, 0x0000U) && pass;
    pass = hardware_test_ade_check_reg16("COMPMODE", ADE9153A_REG_COMPMODE, 0x0005U) && pass;
    pass = hardware_test_ade_check_reg16("EP_CFG", ADE9153A_REG_EP_CFG, 0x0009U) && pass;
    pass = hardware_test_ade_check_reg16("EGY_TIME", ADE9153A_REG_EGY_TIME, 0x0F9FU) && pass;
    /* TEMP_CFG.START (bit 3) se autolimpia tras iniciar la conversion; TEMP_EN queda en bit 2. */
    pass = hardware_test_ade_check_reg16("TEMP_CFG", ADE9153A_REG_TEMP_CFG, 0x0004U) && pass;
    pass = hardware_test_ade_check_reg32("CONFIG0", ADE9153A_REG_CONFIG0, 0x00000000U) && pass;
    pass = hardware_test_ade_check_reg32("VLEVEL", ADE9153A_REG_VLEVEL, 0x002C11E8U) && pass;
    pass = hardware_test_ade_check_reg32("VDIV_RSMALL", ADE9153A_REG_VDIV_RSMALL, 1000U) && pass;

    return pass;
}

static esp_err_t hardware_test_ade_read_sample(hardware_test_ade_sample_t *sample)
{
    if (sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)memset(sample, 0, sizeof(*sample));

    esp_err_t err = ade9153a_read_reg32(ADE9153A_REG_AIRMS, &sample->airms);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_read_reg32(ADE9153A_REG_AVRMS, &sample->avrms);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_read_reg32(ADE9153A_REG_AWATT, &sample->awatt);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_read_reg32(ADE9153A_REG_AVA, &sample->ava);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_read_reg32(ADE9153A_REG_AFVAR, &sample->afvar);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_read_reg32(ADE9153A_REG_APF, &sample->apf);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_read_reg32(ADE9153A_REG_APERIOD, &sample->aperiod);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_read_reg32(ADE9153A_REG_AWATTHR_HI, &sample->awatthr_hi);
    if (err != ESP_OK) {
        return err;
    }
    err = ade9153a_read_reg32(ADE9153A_REG_AWATTHR_LO, &sample->awatthr_lo);
    if (err != ESP_OK) {
        return err;
    }

    sample->vrms_v = voltage_get_vrms();
    sample->irms_a = current_get_irms_a();
    sample->active_w = active_power_get_watts();
    sample->reactive_var = reactive_power_get_var();
    sample->apparent_va = apparent_power_get_va();
    sample->power_factor = power_factor_get();
    sample->frequency_hz = voltage_get_frequency();
    sample->active_wh = active_power_get_wh();
    sample->ac_valid = (sample->avrms >= METER_AC_PRESENT_VRMS_RAW_ON_MIN) &&
                       (sample->frequency_hz >= METER_AC_PRESENT_FREQ_MIN_HZ) &&
                       (sample->frequency_hz <= METER_AC_PRESENT_FREQ_MAX_HZ);
    return temperature_read_internal(&sample->temperature_c);
}

static void hardware_test_ade_log_sample(uint32_t index,
                                         const hardware_test_ade_sample_t *sample,
                                         esp_err_t err)
{
    if ((sample == NULL) || (err != ESP_OK)) {
        LOG_ERROR(TAG, "ADE MEAS sample=%lu read_error=%s",
                  (unsigned long)index, esp_err_to_name(err));
        return;
    }

    LOG_INFO(TAG,
             "ADE_RAW,%lu,%08lx,%08lx,%08lx,%08lx,%08lx,%08lx,%08lx,%08lx,%08lx",
             (unsigned long)index,
             (unsigned long)sample->avrms, (unsigned long)sample->airms,
             (unsigned long)sample->awatt, (unsigned long)sample->ava,
             (unsigned long)sample->afvar, (unsigned long)sample->apf,
             (unsigned long)sample->aperiod,
             (unsigned long)sample->awatthr_hi,
             (unsigned long)sample->awatthr_lo);
    LOG_INFO(TAG,
             "ADE_SI,%lu,%u,%.4f,%.6f,%.4f,%.4f,%.4f,%.5f,%.4f,%.9f,%.2f",
             (unsigned long)index, (unsigned)sample->ac_valid,
             (double)sample->vrms_v,
             (double)sample->irms_a, (double)sample->active_w,
             (double)sample->reactive_var, (double)sample->apparent_va,
             (double)sample->power_factor, (double)sample->frequency_hz,
             (double)sample->active_wh, (double)sample->temperature_c);
}

static void hardware_test_run_ade_measurement(void)
{
    LOG_WARN(TAG, "PRUEBA ADE9153A DE CONFIGURACION Y MEDICION; microSD debe estar desconectada");
    LOG_WARN(TAG, "FASE ACTUAL SIN AC: LINEA, NEUTRO Y CARGA deben permanecer desconectados");
    LOG_INFO(TAG, "ADE_RAW,n,AVRMS,AIRMS,AWATT,AVA,AFVAR,APF,APERIOD,AWATTHR_HI,AWATTHR_LO");
    LOG_INFO(TAG, "ADE_SI,n,AC_VALID,Vrms_V,Irms_A,P_W,Q_VAR,S_VA,PF,F_Hz,E_Wh,T_C");

    const gpio_config_t cs_cfg = {
        .pin_bit_mask = (1ULL << SD_PIN_CS) | (1ULL << ADE9153A_PIN_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cs_cfg);
    if (err == ESP_OK) {
        err = gpio_set_level(SD_PIN_CS, 1);
    }
    if (err == ESP_OK) {
        err = gpio_set_level(ADE9153A_PIN_CS, 1);
    }
    if (err == ESP_OK) {
        err = ade9153a_spi_init();
    }
    if (err == ESP_OK) {
        err = ade9153a_init();
    }
    if (err != ESP_OK) {
        hardware_test_log_result("ADE MEAS INIT", false,
                                 "fallo GPIO/SPI/reset/configuracion del ADE");
        for (;;) {
            hardware_test_delay(HW_TEST_RETRY_MS);
        }
    }

    ade9153a_diag_t diag = {0};
    err = ade9153a_get_diag(&diag);
    const bool identity_ok = (err == ESP_OK) && diag.ready && !diag.bus_disconnected;
    hardware_test_log_result("ADE MEAS ID", identity_ok,
                             "identidad y RUN leidos antes de medir");
    if (!identity_ok) {
        for (;;) {
            hardware_test_delay(HW_TEST_RETRY_MS);
        }
    }

    const bool config_ok = hardware_test_ade_validate_config();
    hardware_test_log_result("ADE CONFIG READBACK", config_ok,
                             config_ok ? "14/14 registros coinciden"
                                       : "uno o mas registros no coinciden");

    bool modules_ok = true;
    modules_ok = (voltage_init(TARGET_AVCC) == ESP_OK) && modules_ok;
    modules_ok = (current_init(TARGET_AICC, TARGET_AICC) == ESP_OK) && modules_ok;
    modules_ok = (active_power_init(TARGET_WCC, TARGET_WHCC) == ESP_OK) && modules_ok;
    modules_ok = (reactive_power_init(TARGET_WCC, TARGET_WHCC) == ESP_OK) && modules_ok;
    modules_ok = (apparent_power_init(TARGET_WCC, TARGET_WHCC) == ESP_OK) && modules_ok;
    modules_ok = (power_factor_init() == ESP_OK) && modules_ok;
    modules_ok = (temperature_init() == ESP_OK) && modules_ok;
    hardware_test_log_result("ADE CONVERSION MODULES", modules_ok,
                             "constantes ADI cargadas para la EV-ADE9153ASHIELDZ");
    if (!modules_ok) {
        for (;;) {
            hardware_test_delay(HW_TEST_RETRY_MS);
        }
    }

    LOG_INFO(TAG, "ADE DSP settling: 2000 ms antes de iniciar las 30 muestras");
    hardware_test_delay(2000U);

    bool samples_ok = true;
    for (uint32_t i = 1U; i <= HW_TEST_ADE_MEASUREMENT_SAMPLES; ++i) {
        hardware_test_ade_sample_t sample = {0};
        hardware_test_delay(1000U);
        err = hardware_test_ade_read_sample(&sample);
        hardware_test_ade_log_sample(i, &sample, err);
        if (err != ESP_OK) {
            samples_ok = false;
        }
    }

    hardware_test_log_result("ADE MEASUREMENT READS", samples_ok,
                             samples_ok ? "30/30 grupos de registros leidos; AC no requerida"
                                        : "al menos una lectura SPI fallo");
    hardware_test_log_result("ADE_MEASUREMENT", config_ok && samples_ok,
                             (config_ok && samples_ok)
                                 ? "configuracion y adquisicion digital aprobadas"
                                 : "revisar CONFIG READBACK o MEASUREMENT READS");

    uint32_t live_index = HW_TEST_ADE_MEASUREMENT_SAMPLES;
    for (;;) {
        hardware_test_ade_sample_t sample = {0};
        hardware_test_delay(HW_TEST_RETRY_MS);
        ++live_index;
        err = hardware_test_ade_read_sample(&sample);
        hardware_test_ade_log_sample(live_index, &sample, err);
    }
}

typedef struct {
    const char *name;
    uint16_t address;
} hardware_test_ade_reg32_t;

static bool hardware_test_ade_read_all_available_registers(void)
{
    static const hardware_test_ade_reg32_t registers[] = {
        {"AIRMS", ADE9153A_REG_AIRMS}, {"BIRMS", ADE9153A_REG_BIRMS},
        {"AVRMS", ADE9153A_REG_AVRMS}, {"AWATT", ADE9153A_REG_AWATT},
        {"AFVAR", ADE9153A_REG_AFVAR}, {"AVA", ADE9153A_REG_AVA},
        {"APF", ADE9153A_REG_APF}, {"APERIOD", ADE9153A_REG_APERIOD},
        {"AWATTHR_HI", ADE9153A_REG_AWATTHR_HI},
        {"AWATTHR_LO", ADE9153A_REG_AWATTHR_LO},
        {"AFVARHR_HI", ADE9153A_REG_AFVARHR_HI},
        {"AFVARHR_LO", ADE9153A_REG_AFVARHR_LO},
        {"AVAHR_HI", ADE9153A_REG_AVAHR_HI},
        {"AVAHR_LO", ADE9153A_REG_AVAHR_LO},
        {"AIGAIN", ADE9153A_REG_AIGAIN}, {"BIGAIN", ADE9153A_REG_BIGAIN},
        {"AVGAIN", ADE9153A_REG_AVGAIN}, {"APGAIN", ADE9153A_REG_APGAIN},
        {"APHASECAL", ADE9153A_REG_APHASECAL},
        {"TEMP_TRIM", ADE9153A_REG_TEMP_TRIM},
        {"MS_STATUS", ADE9153A_REG_MS_STATUS_CURRENT},
        {"MS_AICC", ADE9153A_REG_MS_ACAL_AICC},
        {"MS_AICERT", ADE9153A_REG_MS_ACAL_AICERT},
        {"MS_BICC", ADE9153A_REG_MS_ACAL_BICC},
        {"MS_BICERT", ADE9153A_REG_MS_ACAL_BICERT},
        {"MS_AVCC", ADE9153A_REG_MS_ACAL_AVCC},
        {"MS_AVCERT", ADE9153A_REG_MS_ACAL_AVCERT},
    };

    bool pass = true;
    for (size_t i = 0U; i < (sizeof(registers) / sizeof(registers[0])); ++i) {
        uint32_t value = 0U;
        const esp_err_t err = ade9153a_read_reg32(registers[i].address, &value);
        LOG_INFO(TAG, "ADE REG %-12s addr=0x%04x value=0x%08lx err=%s",
                 registers[i].name, (unsigned)registers[i].address,
                 (unsigned long)value, esp_err_to_name(err));
        if (err != ESP_OK) {
            pass = false;
        }
    }
    return pass;
}

static bool hardware_test_ade_test_access_contracts(void)
{
    uint16_t version16 = 0U;
    uint8_t version8 = 0U;
    uint16_t accmode = 0U;
    uint16_t accmode_after = 0U;
    uint32_t config0 = 0U;
    uint32_t config0_after = 0U;
    bool pass = (ade9153a_read_reg16(ADE9153A_REG_VERSION, &version16) == ESP_OK);
    pass = (ade9153a_read_reg8(ADE9153A_REG_VERSION, &version8) == ESP_OK) && pass;
    pass = (version8 == (uint8_t)(version16 & 0x00FFU)) && pass;

    /* Contratos de argumentos de las tres rutas de lectura. */
    pass = (ade9153a_read_reg8(ADE9153A_REG_VERSION, NULL) == ESP_ERR_INVALID_ARG) && pass;
    pass = (ade9153a_read_reg16(ADE9153A_REG_VERSION, NULL) == ESP_ERR_INVALID_ARG) && pass;
    pass = (ade9153a_read_reg32(ADE9153A_REG_CONFIG0, NULL) == ESP_ERR_INVALID_ARG) && pass;
    pass = (ade9153a_get_diag(NULL) == ESP_ERR_INVALID_ARG) && pass;

    /* Escritura directa 16/32 bits sin alterar el estado: leer, reescribir y
     * confirmar exactamente el mismo valor. */
    pass = (ade9153a_read_reg16(ADE9153A_REG_ACCMODE, &accmode) == ESP_OK) && pass;
    pass = (ade9153a_write_reg16(ADE9153A_REG_ACCMODE, accmode) == ESP_OK) && pass;
    pass = (ade9153a_read_reg16(ADE9153A_REG_ACCMODE, &accmode_after) == ESP_OK) && pass;
    pass = (accmode_after == accmode) && pass;
    pass = (ade9153a_read_reg32(ADE9153A_REG_CONFIG0, &config0) == ESP_OK) && pass;
    pass = (ade9153a_write_reg32(ADE9153A_REG_CONFIG0, config0) == ESP_OK) && pass;
    pass = (ade9153a_read_reg32(ADE9153A_REG_CONFIG0, &config0_after) == ESP_OK) && pass;
    pass = (config0_after == config0) && pass;

    /* Escrituras idempotentes: conservan la configuracion nominal. */
    pass = (ade9153a_set_pgagain(0x0AU, 0x00U) == ESP_OK) && pass;
    pass = (ade9153a_set_line_freq((uint8_t)METER_LINE_FREQ_HZ) == ESP_OK) && pass;
    pass = (ade9153a_set_vlevel(METER_VNOM_V, 1.5f) == ESP_OK) && pass;

    /* Argumentos invalidos no deben cambiar la configuracion. */
    pass = (ade9153a_set_line_freq(0U) == ESP_ERR_INVALID_ARG) && pass;
    pass = (ade9153a_set_vlevel(0.0f, 1.0f) == ESP_ERR_INVALID_ARG) && pass;
    pass = ade9153a_is_ready() && pass;

    LOG_INFO(TAG,
             "ADE ACCESS VERSION16=0x%04x VERSION8=0x%02x ACCMODE=0x%04x CONFIG0=0x%08lx",
             (unsigned)version16, (unsigned)version8, (unsigned)accmode_after,
             (unsigned long)config0_after);
    return pass && hardware_test_ade_validate_config();
}

static bool hardware_test_ade_init_conversion_modules(void)
{
    bool pass = true;
    pass = (voltage_init(TARGET_AVCC) == ESP_OK) && pass;
    pass = (current_init(TARGET_AICC, TARGET_AICC) == ESP_OK) && pass;
    pass = (active_power_init(TARGET_WCC, TARGET_WHCC) == ESP_OK) && pass;
    pass = (reactive_power_init(TARGET_WCC, TARGET_WHCC) == ESP_OK) && pass;
    pass = (apparent_power_init(TARGET_WCC, TARGET_WHCC) == ESP_OK) && pass;
    pass = (power_factor_init() == ESP_OK) && pass;
    pass = (temperature_init() == ESP_OK) && pass;
    return pass;
}

static bool hardware_test_ade_test_conversions(void)
{
    uint32_t vrms_raw = 0U;
    uint32_t period_raw = 0U;
    const esp_err_t vrms_raw_err = voltage_get_vrms_raw(&vrms_raw);
    const esp_err_t period_raw_err = voltage_get_aperiod_raw(&period_raw);

    const float vrms = voltage_get_vrms();
    const float vpeak = voltage_get_vpeak();
    const float frequency = voltage_get_frequency();
    const float irms_a = current_get_irms_a();
    const float irms_b = current_get_irms_b();
    const float active_w = active_power_get_watts();
    const float reactive_var = reactive_power_get_var();
    const float apparent_va = apparent_power_get_va();
    const float pf = power_factor_get();
    const float active_wh = active_power_get_wh();
    const int32_t active_wh_hi = active_power_get_wh_raw_hi();
    const float reactive_varh = reactive_power_get_varh();
    const float apparent_vah = apparent_power_get_vah();
    float temperature_c = 0.0f;
    const esp_err_t temperature_err = temperature_read_internal(&temperature_c);
    const float temperature_legacy_c = temperature_get_internal();

    const bool numeric_ok =
        isfinite(vrms) && (vrms >= 0.0f) && (vrms <= METER_VRMS_SANITY_MAX_V) &&
        isfinite(frequency) && (frequency >= 0.0f) &&
        isfinite(irms_a) && (irms_a >= 0.0f) && (irms_a <= (METER_IMAX_A * 2.0f)) &&
        isfinite(irms_b) && (irms_b >= 0.0f) && (irms_b <= (METER_IMAX_A * 2.0f)) &&
        isfinite(active_w) && (fabsf(active_w) <= METER_ACTIVE_POWER_SANITY_MAX_W) &&
        isfinite(reactive_var) && (fabsf(reactive_var) <= METER_REACTIVE_POWER_SANITY_MAX_VAR) &&
        isfinite(apparent_va) && (apparent_va >= 0.0f) &&
        (apparent_va <= METER_APPARENT_POWER_SANITY_MAX_VA) &&
        isfinite(pf) && (pf >= -1.0f) && (pf <= 1.0f) &&
        isfinite(active_wh) && isfinite(reactive_varh) && isfinite(apparent_vah) &&
        isfinite(temperature_legacy_c) &&
        (temperature_legacy_c >= METER_TEMP_SANITY_MIN_C) &&
        (temperature_legacy_c <= METER_TEMP_SANITY_MAX_C);

    LOG_INFO(TAG,
             "ADE ALL SI Vrms=%.4f Vpeak=%.4f F=%.4f IrmsA=%.6f IrmsB=%.6f P=%.4f Q=%.4f S=%.4f PF=%.5f Wh=%.9f VARh=%.9f VAh=%.9f T=%.2f Tlegacy=%.2f",
             (double)vrms, (double)vpeak, (double)frequency,
             (double)irms_a, (double)irms_b, (double)active_w,
             (double)reactive_var, (double)apparent_va, (double)pf,
             (double)active_wh, (double)reactive_varh,
             (double)apparent_vah, (double)temperature_c,
             (double)temperature_legacy_c);
    LOG_INFO(TAG,
             "ADE ALL RAW AVRMS=0x%08lx APERIOD=0x%08lx AWATTHR_HI=0x%08lx",
             (unsigned long)vrms_raw, (unsigned long)period_raw,
             (unsigned long)(uint32_t)active_wh_hi);
    LOG_WARN(TAG, "ADE VPEAK NOT_IMPLEMENTED: API=%.4f; no se valida como medicion",
             (double)vpeak);
    LOG_WARN(TAG, "ADE B channel DISABLED_BY_CONFIG: METER_BI_PGAGAIN=%d",
             METER_BI_PGAGAIN);

    return (vrms_raw_err == ESP_OK) && (period_raw_err == ESP_OK) &&
           (temperature_err == ESP_OK) && numeric_ok;
}

static bool hardware_test_ade_test_recovery(void)
{
    static const ade9153a_recovery_action_t actions[] = {
        ADE9153A_RECOVERY_ACTION_PROBE,
        ADE9153A_RECOVERY_ACTION_SOFT_INIT,
        ADE9153A_RECOVERY_ACTION_SPI_REINIT,
    };

    bool pass = true;
    for (size_t i = 0U; i < (sizeof(actions) / sizeof(actions[0])); ++i) {
        const esp_err_t err = ade9153a_recover(actions[i]);
        ade9153a_diag_t diag = {0};
        const esp_err_t diag_err = ade9153a_get_diag(&diag);
        const bool action_ok = (err == ESP_OK) && (diag_err == ESP_OK) && diag.ready;
        LOG_INFO(TAG,
                 "ADE RECOVERY action=%u ready=%u count=%lu err=%s diag=%s",
                 (unsigned)actions[i], (unsigned)diag.ready,
                 (unsigned long)diag.recovery_count,
                 esp_err_to_name(err), esp_err_to_name(diag_err));
        pass = action_ok && pass;
        if ((actions[i] != ADE9153A_RECOVERY_ACTION_PROBE) && action_ok) {
            hardware_test_delay(2000U);
            pass = hardware_test_ade_validate_config() && pass;
        }
    }
    return pass;
}

static bool hardware_test_ade_test_msure_interface(void)
{
    msure_result_t result = {0};
    msure_result_t valid_result = {
        .aicc_nA_per_code = TARGET_AICC,
        .avcc_nV_per_code = TARGET_AVCC,
        .aicert_raw = 0,
        .avcert_raw = 0,
    };
    msure_result_t invalid_result = valid_result;
    invalid_result.aicert_raw = METER_MSURE_CERT_PPM + 1;
    const esp_err_t init_err = msure_init();
    const esp_err_t invalid_err = msure_run(false, false,
                                             MSURE_MODE_AI_NORMAL, &result);
    uint32_t status = 0U;
    const esp_err_t status_err = ade9153a_read_reg32(
        ADE9153A_REG_MS_STATUS_CURRENT, &status);
    LOG_INFO(TAG, "ADE mSure interface status=0x%08lx init=%s invalid_contract=%s",
             (unsigned long)status, esp_err_to_name(init_err),
             esp_err_to_name(invalid_err));
    LOG_WARN(TAG,
             "ADE mSure AUTOCAL EXECUTION DEFERRED: modifica AIGAIN/AVGAIN y requiere protocolo propio");
    return (init_err == ESP_OK) && (invalid_err == ESP_ERR_INVALID_ARG) &&
           (status_err == ESP_OK) &&
           !msure_cert_is_valid(NULL) && !msure_result_is_plausible(NULL) &&
           msure_cert_is_valid(&valid_result) &&
           msure_result_is_plausible(&valid_result) &&
           !msure_cert_is_valid(&invalid_result);
}

static bool hardware_test_ade_test_power_quality(void)
{
    bool pass = (sag_swell_init() == ESP_OK);

    const float nominal = METER_VNOM_V;
    pass = (sag_swell_eval(nominal, true) == 0U) && pass;
    pass = ((sag_swell_eval(nominal * 0.85f, true) & SAG_SWELL_FLAG_SAG) != 0U) && pass;
    pass = (sag_swell_eval(nominal, true) == 0U) && pass;
    pass = ((sag_swell_eval(nominal * 1.15f, true) & SAG_SWELL_FLAG_SWELL) != 0U) && pass;
    pass = (sag_swell_eval(nominal, true) == 0U) && pass;
    pass = (sag_swell_eval(nominal * 0.85f, false) == 0U) && pass;

    pass = (pq_monitor_init() == ESP_OK) && pass;
    MeterData_t snap = {
        .vrms = nominal,
        .frequency = (float)METER_LINE_FREQ_HZ,
        .temperature = 25.0f,
    };
    pass = (pq_monitor_process_snapshot(NULL) == 0U) && pass;
    pass = (pq_monitor_process_snapshot(&snap) == 0U) && pass;

    snap.vrms = nominal * 0.85f;
    pass = ((pq_monitor_process_snapshot(&snap) & PQ_FLAG_SAG) != 0U) && pass;
    snap.vrms = nominal;
    pass = (pq_monitor_process_snapshot(&snap) == 0U) && pass;
    snap.vrms = nominal * 1.15f;
    pass = ((pq_monitor_process_snapshot(&snap) & PQ_FLAG_SWELL) != 0U) && pass;
    snap.vrms = nominal;
    snap.frequency = METER_AC_PRESENT_FREQ_MAX_HZ + 5.0f;
    pass = ((pq_monitor_process_snapshot(&snap) & PQ_FLAG_FREQ_OOR) != 0U) && pass;
    snap.frequency = (float)METER_LINE_FREQ_HZ;
    snap.temperature = METER_OVERTEMP_C + 1.0f;
    const uint32_t overtemp_flags = pq_monitor_process_snapshot(&snap);
    pass = ((overtemp_flags & PQ_FLAG_OVERTEMP) != 0U) && pass;
    pass = (pq_monitor_get_flags() == overtemp_flags) && pass;

    return pass;
}

static void hardware_test_run_ade_full(void)
{
    LOG_WARN(TAG, "PRUEBA INTEGRAL ADE9153A; ejecutar sin carga y con microSD desconectada");
    LOG_WARN(TAG, "La ejecucion mSure real queda excluida porque modifica ganancias de calibracion");

    const gpio_config_t cs_cfg = {
        .pin_bit_mask = (1ULL << SD_PIN_CS) | (1ULL << ADE9153A_PIN_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cs_cfg);
    if (err == ESP_OK) {
        err = gpio_set_level(SD_PIN_CS, 1);
    }
    if (err == ESP_OK) {
        err = gpio_set_level(ADE9153A_PIN_CS, 1);
    }
    if (err == ESP_OK) {
        err = ade9153a_spi_init();
    }
    if (err == ESP_OK) {
        err = ade9153a_init();
    }
    if (err != ESP_OK) {
        hardware_test_log_result("ADE FULL INIT", false,
                                 "fallo GPIO/SPI/reset/configuracion");
        for (;;) {
            hardware_test_delay(HW_TEST_RETRY_MS);
        }
    }

    ade9153a_diag_t diag = {0};
    const bool identity_ok = (ade9153a_get_diag(&diag) == ESP_OK) && diag.ready;
    hardware_test_log_result("ADE FULL ID", identity_ok,
                             "RUN, VERSION, PRODUCT y CHIP_ID");

    const bool config_ok = hardware_test_ade_validate_config();
    hardware_test_log_result("ADE FULL CONFIG", config_ok,
                             "14 registros criticos verificados");

    const bool access_ok = hardware_test_ade_test_access_contracts();
    hardware_test_log_result("ADE FULL ACCESS", access_ok,
                             "lectura 8/16/32 y escrituras idempotentes");

    const bool register_ok = hardware_test_ade_read_all_available_registers();
    hardware_test_log_result("ADE FULL REGISTERS", register_ok,
                             "27 registros de medicion/calibracion accesibles");

    const bool modules_ok = hardware_test_ade_init_conversion_modules();
    hardware_test_log_result("ADE FULL MODULE INIT", modules_ok,
                             "V/I/P/Q/S/PF/energia/temperatura");

    hardware_test_delay(2000U);
    const bool conversions_ok = modules_ok && hardware_test_ade_test_conversions();
    hardware_test_log_result("ADE FULL CONVERSIONS", conversions_ok,
                             "API implementadas retornan valores finitos");

    const bool msure_ok = hardware_test_ade_test_msure_interface();
    hardware_test_log_result("ADE FULL MSURE INTERFACE", msure_ok,
                             "interfaz/estado legibles; autocalibracion diferida");

    const bool pq_ok = hardware_test_ade_test_power_quality();
    hardware_test_log_result("ADE FULL POWER QUALITY", pq_ok,
                             "sag, swell, frecuencia y sobretemperatura");

    const esp_err_t harmonics_err = harmonics_init();
    const bool harmonics_stub_ok = (harmonics_err == ESP_ERR_NOT_SUPPORTED);
    hardware_test_log_result("ADE FULL HARMONICS", harmonics_stub_ok,
                             "NOT_IMPLEMENTED esperado; no altera resultado global");

    const bool recovery_ok = hardware_test_ade_test_recovery();
    hardware_test_log_result("ADE FULL RECOVERY", recovery_ok,
                             "PROBE, SOFT_INIT y SPI_REINIT");

    const bool post_modules_ok = hardware_test_ade_init_conversion_modules();
    hardware_test_delay(2000U);
    const bool post_recovery_ok = post_modules_ok &&
                                  hardware_test_ade_test_conversions();
    hardware_test_log_result("ADE FULL POST-RECOVERY", post_recovery_ok,
                             "mediciones disponibles tras recuperacion");

    const bool overall = identity_ok && config_ok && access_ok && register_ok &&
                         modules_ok && conversions_ok && msure_ok && pq_ok &&
                         harmonics_stub_ok && recovery_ok && post_recovery_ok;
    hardware_test_log_result("ADE_FULL", overall,
                             overall ? "funciones implementadas aprobadas; ver exclusiones"
                                     : "revisar subresultado FAIL");

    for (;;) {
        hardware_test_delay(HW_TEST_RETRY_MS);
        (void)hardware_test_ade_test_conversions();
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
    if (strcmp(label, "SIGNAL") == 0) {
        const char *csq = strstr(response, "+CSQ:");
        if (csq != NULL) {
            csq += strlen("+CSQ:");
            while (*csq == ' ') {
                ++csq;
            }
            size_t n = 0U;
            while ((csq[n] != '\0') && (csq[n] != ' ') &&
                   (n < (sizeof(s_sim_at_csq) - 1U))) {
                s_sim_at_csq[n] = csq[n];
                ++n;
            }
            s_sim_at_csq[n] = '\0';
        } else {
            (void)snprintf(s_sim_at_csq, sizeof(s_sim_at_csq), "--");
        }
    }
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
    s_sim_at_stage = "INIT";
    esp_err_t err = sim7080g_init();
    if (err != ESP_OK) {
        s_sim_at_stage = sim7080g_last_at_response_ok() ? "MODEM_INIT" : "NO_AT";
        return err;
    }

    s_sim_at_stage = "ATI";
    if (hardware_test_sim_query("IDENT", "ATI", NULL) != ESP_OK) {
        return ESP_FAIL;
    }
    s_sim_at_stage = "CPIN";
    if (hardware_test_sim_query("SIM", "AT+CPIN?", "+CPIN: READY") != ESP_OK) {
        return ESP_FAIL;
    }
    s_sim_at_stage = "CSQ";
    if (hardware_test_sim_query("SIGNAL", "AT+CSQ", "+CSQ:") != ESP_OK) {
        return ESP_FAIL;
    }
    s_sim_at_stage = "IMEI";
    err = sim7080g_read_imei();
    if (err != ESP_OK) {
        return err;
    }
    LOG_INFO(TAG, "SIM IMEI PASS value=%s", sim7080g_get_imei());
    s_sim_at_stage = "OK";
    return ESP_OK;
}

static void hardware_test_sim_at_render(bool oled_ready,
                                        bool pass,
                                        uint32_t pass_count,
                                        uint32_t fail_count)
{
    if (!oled_ready) {
        return;
    }

    char line[32];
    (void)oled_driver_clear();
    (void)oled_driver_draw_text(0U, "SIM AT AISLADO");
    (void)oled_driver_draw_text(1U, pass ? "ESTADO: PASS" : "ESTADO: FAIL");
    (void)snprintf(line, sizeof(line), "ETAPA: %s", s_sim_at_stage);
    (void)oled_driver_draw_text(2U, line);
    (void)snprintf(line, sizeof(line), "CSQ: %s", s_sim_at_csq);
    (void)oled_driver_draw_text(3U, line);
    (void)snprintf(line, sizeof(line), "CICLOS OK: %lu",
                   (unsigned long)pass_count);
    (void)oled_driver_draw_text(4U, line);
    (void)snprintf(line, sizeof(line), "CICLOS FAIL: %lu",
                   (unsigned long)fail_count);
    (void)oled_driver_draw_text(5U, line);
    (void)oled_driver_draw_text(6U, "SIN MQTT/SD/ADE");
    (void)oled_driver_draw_text(7U, "NO TOCAR PWRKEY");
    (void)oled_driver_flush();
}

static void hardware_test_run_sim_at(void)
{
    const bool oled_ready = (oled_driver_init() == ESP_OK);
    if (!oled_ready) {
        LOG_WARN(TAG, "OLED no disponible; SIM_AT continuara por salida serial");
    }
    uint32_t pass_count = 0U;
    uint32_t fail_count = 0U;
    for (;;) {
        const esp_err_t err = hardware_test_sim_basic();
        if (err == ESP_OK) {
            ++pass_count;
            hardware_test_log_result("SIM_AT", true,
                                     "UART, AT, SIM, CSQ e IMEI respondieron");
        } else {
            ++fail_count;
            hardware_test_log_result("SIM_AT", false,
                                     "ver etapa OLED; revisar riel, UART y estado SIM");
        }
        hardware_test_sim_at_render(oled_ready, err == ESP_OK,
                                    pass_count, fail_count);
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
    uint32_t network_sessions = 0U;
    uint32_t network_drops = 0U;
    int64_t outage_start_us = 0LL;

    LOG_INFO(TAG, "NETDIAG firmware=%s profile=test_sim_network_long; MQTT/ADE/SD excluded",
             METER_FW_VERSION);

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

        ++network_sessions;
        const int64_t ready_us = esp_timer_get_time();
        const uint32_t outage_ms = (outage_start_us > 0LL)
                                       ? (uint32_t)((ready_us - outage_start_us) / 1000LL)
                                       : 0U;
        outage_start_us = 0LL;
        LOG_INFO(TAG,
                 "NETDIAG READY session=%lu drops=%lu uptime_s=%llu outage_ms=%lu",
                 (unsigned long)network_sessions,
                 (unsigned long)network_drops,
                 (unsigned long long)(ready_us / 1000000LL),
                 (unsigned long)outage_ms);
        (void)hardware_test_sim_query("REG", "AT+CEREG?", "+CEREG:");
        (void)hardware_test_sim_query("OPERATOR", "AT+COPS?", "+COPS:");
        (void)hardware_test_sim_query("PDP", "AT+CNACT?", "+CNACT:");
        (void)hardware_test_sim_query("RADIO", "AT+CPSI?", "+CPSI:");
        hardware_test_log_result("SIM_NETWORK", true,
                                 "registro celular y contexto PDP activos");

        uint32_t status_samples = 0U;
        for (;;) {
            hardware_test_delay(HW_TEST_SIM_STATUS_PERIOD_MS);
            err = sim7080g_refresh_runtime_state();
            if (err != ESP_OK) {
                ++network_drops;
                outage_start_us = esp_timer_get_time();
                LOG_ERROR(TAG,
                          "NETDIAG DROP event=%lu uptime_s=%llu stage=refresh_runtime_state err=%s",
                          (unsigned long)network_drops,
                          (unsigned long long)(outage_start_us / 1000000LL),
                          esp_err_to_name(err));
                (void)hardware_test_sim_query("FAIL_REG", "AT+CEREG?", "+CEREG:");
                (void)hardware_test_sim_query("FAIL_OP", "AT+COPS?", "+COPS:");
                (void)hardware_test_sim_query("FAIL_ATTACH", "AT+CGATT?", "+CGATT:");
                (void)hardware_test_sim_query("FAIL_PDP", "AT+CNACT?", "+CNACT:");
                (void)hardware_test_sim_query("FAIL_RADIO", "AT+CPSI?", "+CPSI:");
                hardware_test_log_result("SIM_NETWORK", false,
                                         "se perdio red o PDP durante monitoreo");
                break;
            }
            (void)hardware_test_sim_query("SIGNAL", "AT+CSQ", "+CSQ:");
            ++status_samples;
            if (status_samples >= 20U) {
                status_samples = 0U;
                (void)hardware_test_sim_query("REG", "AT+CEREG?", "+CEREG:");
                (void)hardware_test_sim_query("OPERATOR", "AT+COPS?", "+COPS:");
                (void)hardware_test_sim_query("PDP", "AT+CNACT?", "+CNACT:");
                (void)hardware_test_sim_query("RADIO", "AT+CPSI?", "+CPSI:");
            }
            hardware_test_log_result("SIM_NETWORK", true,
                                     "red y PDP siguen activos");
        }
    }
}

static bool hardware_test_sim_tcp_open_result(const char *response,
                                              int *result)
{
    if ((response == NULL) || (result == NULL)) {
        return false;
    }
    const char *line = strstr(response, "+CAOPEN:");
    if (line == NULL) {
        return false;
    }
    int cid = -1;
    if (sscanf(line + strlen("+CAOPEN:"), " %d,%d", &cid, result) != 2) {
        return false;
    }
    return cid == 0;
}

static esp_err_t hardware_test_sim_dns_resolve(const char *host,
                                               char *ip,
                                               size_t ip_len)
{
    if ((host == NULL) || (ip == NULL) || (ip_len < 8U)) {
        return ESP_ERR_INVALID_ARG;
    }

    char command[128];
    char response[96] = {0};
    char urc[160] = {0};
    char domain[72] = {0};
    char address[32] = {0};
    int dns_result = -1;

    (void)hardware_test_sim_query("DNS_PDP", "AT+CDNSPDPID?", "+CDNSPDPID:");
    (void)hardware_test_sim_query("DNS_CONFIG", "AT+CDNSCFG?", NULL);

    (void)snprintf(command, sizeof(command),
                   "AT+CDNSGIP=\"%s\",1,3000", host);
    esp_err_t err = sim7080g_hal_send_cmd(command, NULL, response,
                                          sizeof(response), 5000U, 1U);
    hardware_test_response_one_line(response);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "TCPDIAG DNS command failed err=%s resp=%s",
                  esp_err_to_name(err), response);
        return err;
    }

    err = sim7080g_hal_read_urc("+CDNSGIP:", urc, sizeof(urc), 15000U);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "TCPDIAG DNS result URC missing err=%s", esp_err_to_name(err));
        return err;
    }
    hardware_test_response_one_line(urc);

    const char *payload = strchr(urc, ':');
    if ((payload == NULL) ||
        (sscanf(payload + 1, " %d,%71[^,],%31[^,\r\n]",
                &dns_result, domain, address) != 3) ||
        (dns_result != 1)) {
        LOG_ERROR(TAG, "TCPDIAG DNS resolution failed result=%d urc=%s",
                  dns_result, urc);
        return ESP_FAIL;
    }

    char *first = address;
    while ((*first == ' ') || (*first == '\t') || (*first == '"')) {
        ++first;
    }
    char *last = first + strlen(first);
    while ((last > first) && ((last[-1] == ' ') || (last[-1] == '\t') ||
                              (last[-1] == '"'))) {
        --last;
    }
    *last = '\0';
    if ((strlen(first) < 7U) || (strlen(first) >= ip_len)) {
        LOG_ERROR(TAG, "TCPDIAG DNS invalid IPv4 address domain=%s ip=%s",
                  domain, first);
        return ESP_ERR_INVALID_SIZE;
    }

    (void)strncpy(ip, first, ip_len - 1U);
    ip[ip_len - 1U] = '\0';
    LOG_INFO(TAG, "TCPDIAG DNS PASS domain=%s ip=%s", domain, ip);
    return ESP_OK;
}

static void hardware_test_run_sim_tcp(void)
{
    const char *const host = "tesisproject-production.up.railway.app";
    const unsigned port = 443U;
    char tcp_target[64] = {0};
    char command[128];
    char response[256] = {0};
    uint32_t attempts = 0U;
    uint32_t opened = 0U;
    uint32_t failed = 0U;
    uint32_t close_failed = 0U;

    LOG_INFO(TAG,
             "TCPDIAG firmware=%s alternate_target=%s:%u profile=test_sim_tcp; MQTT/ADE/SD excluded",
             METER_FW_VERSION, host, port);
    LOG_WARN(TAG, "TCPDIAG prueba conexiones TCP solamente; no publica telemetria ni MQTT CONNECT");

    esp_err_t err = hardware_test_sim_basic();
    if (err == ESP_OK) {
        err = sim7080g_wait_network(METER_SIM7080G_NETWORK_TIMEOUT_MS);
    }
    if (err == ESP_OK) {
        err = sim7080g_activate_pdp();
    }
    if (err != ESP_OK) {
        hardware_test_log_result("SIM_TCP INIT", false,
                                 "no se obtuvo registro celular o PDP activo");
        for (;;) {
            hardware_test_delay(HW_TEST_RETRY_MS);
        }
    }

    err = sim7080g_hal_send_cmd("AT+CACFG=\"ASYNCOPEN\",0", NULL,
                                response, sizeof(response),
                                METER_SIM7080G_AT_TIMEOUT_MS, 1U);
    hardware_test_response_one_line(response);
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "TCPDIAG configure ASYNCOPEN failed err=%s resp=%s",
                  esp_err_to_name(err), response);
        hardware_test_log_result("SIM_TCP CONFIG", false,
                                 "el modem no acepto apertura TCP sincrona");
        for (;;) {
            hardware_test_delay(HW_TEST_RETRY_MS);
        }
    }
    LOG_INFO(TAG, "TCPDIAG ASYNCOPEN=0 confirmed resp=%s", response);
    (void)hardware_test_sim_query("TCP_CID_CAP", "AT+CACID=?", "+CACID:");
    err = hardware_test_sim_query("TCP_CID", "AT+CACID?", "+CACID: 0");
    if (err != ESP_OK) {
        LOG_ERROR(TAG, "TCPDIAG socket id 0 is not selected");
        hardware_test_log_result("SIM_TCP CONFIG", false,
                                 "el modem no reporto CACID=0");
        for (;;) {
            hardware_test_delay(HW_TEST_RETRY_MS);
        }
    }
    LOG_INFO(TAG, "TCPDIAG using reported CACID=0; skip setter");
    (void)hardware_test_sim_query("TCP_CAP", "AT+CAOPEN=?", "+CAOPEN:");
    (void)hardware_test_sim_query("TCP_STATE", "AT+CASTATE?", NULL);
    (void)hardware_test_sim_query("TCP_OPEN_LIST", "AT+CAOPEN?", NULL);
    (void)hardware_test_sim_query("MQTT_STATE", "AT+SMSTATE?", NULL);
    (void)hardware_test_sim_query("TCP_CONFIG", "AT+CACFG?", NULL);

    LOG_INFO(TAG, "TCPDIAG READY uptime_s=%llu period_ms=%lu timeout_ms=%lu",
             (unsigned long long)(esp_timer_get_time() / 1000000LL),
             (unsigned long)HW_TEST_SIM_TCP_PERIOD_MS,
             (unsigned long)HW_TEST_SIM_TCP_OPEN_TIMEOUT_MS);
    (void)hardware_test_sim_query("REG", "AT+CEREG?", "+CEREG:");
    (void)hardware_test_sim_query("OPERATOR", "AT+COPS?", "+COPS:");
    (void)hardware_test_sim_query("PDP", "AT+CNACT?", "+CNACT:");
    (void)hardware_test_sim_query("RADIO", "AT+CPSI?", "+CPSI:");

    err = hardware_test_sim_dns_resolve(host, tcp_target, sizeof(tcp_target));
    if (err != ESP_OK) {
        hardware_test_log_result("SIM_TCP DNS", false,
                                 "el modem no resolvio el dominio del proxy");
        for (;;) {
            hardware_test_delay(60000U);
        }
    }

    int64_t next_attempt_us = esp_timer_get_time();
    uint32_t full_snapshot_counter = 0U;
    while (attempts < HW_TEST_SIM_TCP_MAX_ATTEMPTS) {
        const int64_t now_us = esp_timer_get_time();
        if (now_us < next_attempt_us) {
            const uint32_t wait_ms = (uint32_t)((next_attempt_us - now_us) / 1000LL);
            hardware_test_delay(wait_ms > 0U ? wait_ms : 1U);
            continue;
        }
        next_attempt_us = esp_timer_get_time() +
                          ((int64_t)HW_TEST_SIM_TCP_PERIOD_MS * 1000LL);
        ++attempts;

        int64_t open_start_us = esp_timer_get_time();
        (void)snprintf(command, sizeof(command),
                       "AT+CAOPEN=0,0,\"TCP\",\"%s\",%u", tcp_target, port);
        response[0] = '\0';
        err = sim7080g_hal_send_cmd(command, "+CAOPEN:", response,
                                    sizeof(response),
                                    HW_TEST_SIM_TCP_OPEN_TIMEOUT_MS, 1U);
        const uint32_t open_ms =
            (uint32_t)((esp_timer_get_time() - open_start_us) / 1000LL);
        hardware_test_response_one_line(response);

        int open_result = -1;
        const bool parsed = hardware_test_sim_tcp_open_result(response,
                                                               &open_result);
        if ((err != ESP_OK) || !parsed || (open_result != 0)) {
            ++failed;
            LOG_ERROR(TAG,
                      "TCPDIAG OPEN FAIL attempt=%lu elapsed_ms=%lu err=%s result=%d parsed=%u resp=%s",
                      (unsigned long)attempts, (unsigned long)open_ms,
                      esp_err_to_name(err), open_result, (unsigned)parsed,
                      response);
            (void)hardware_test_sim_query("FAIL_REG", "AT+CEREG?", "+CEREG:");
            (void)hardware_test_sim_query("FAIL_PDP", "AT+CNACT?", "+CNACT:");
            (void)hardware_test_sim_query("FAIL_RADIO", "AT+CPSI?", "+CPSI:");
            (void)hardware_test_sim_query("FAIL_TCP_STATE", "AT+CASTATE?", NULL);
            (void)hardware_test_sim_query("FAIL_TCP_CID", "AT+CACID?", NULL);
            (void)hardware_test_sim_query("FAIL_TCP_OPEN_LIST", "AT+CAOPEN?", NULL);
            (void)hardware_test_sim_query("FAIL_TCP_CONFIG", "AT+CACFG?", NULL);
            (void)hardware_test_sim_query("FAIL_MQTT_STATE", "AT+SMSTATE?", NULL);
            (void)hardware_test_sim_query("FAIL_TCP_CAP", "AT+CAOPEN=?", "+CAOPEN:");
            (void)hardware_test_sim_query("FAIL_CGACT", "AT+CGACT?", NULL);
            (void)hardware_test_sim_query("FAIL_CGDCONT", "AT+CGDCONT?", NULL);
            (void)hardware_test_sim_query("FAIL_CDNS_PDP", "AT+CDNSPDPID?", "+CDNSPDPID:");
            (void)hardware_test_sim_query("FAIL_CDNS_CONFIG", "AT+CDNSCFG?", NULL);
            ++full_snapshot_counter;
            hardware_test_log_result("SIM_TCP", false,
                                     "la apertura TCP al destino alterno no fue confirmada");
            continue;
        }

        ++opened;
        bool cycle_ok = true;
        LOG_INFO(TAG,
                 "TCPDIAG OPEN PASS attempt=%lu opened=%lu failed=%lu elapsed_ms=%lu result=0 resp=%s",
                 (unsigned long)attempts, (unsigned long)opened,
                 (unsigned long)failed, (unsigned long)open_ms, response);

        response[0] = '\0';
        err = sim7080g_hal_send_cmd("AT+CASTATE?", NULL, response,
                                    sizeof(response),
                                    METER_SIM7080G_AT_TIMEOUT_MS, 1U);
        hardware_test_response_one_line(response);
        if ((err == ESP_OK) && (strstr(response, "+CASTATE: 0,1") != NULL)) {
            LOG_INFO(TAG, "TCPDIAG STATE PASS resp=%s", response);
        } else {
            cycle_ok = false;
            ++failed;
            LOG_ERROR(TAG, "TCPDIAG STATE FAIL err=%s resp=%s",
                      esp_err_to_name(err), response);
        }

        response[0] = '\0';
        err = sim7080g_hal_send_cmd("AT+CACLOSE=0", NULL, response,
                                    sizeof(response),
                                    METER_SIM7080G_AT_TIMEOUT_MS, 1U);
        hardware_test_response_one_line(response);
        if (err == ESP_OK) {
            LOG_INFO(TAG, "TCPDIAG CLOSE PASS attempt=%lu resp=%s",
                     (unsigned long)attempts, response);
        } else {
            cycle_ok = false;
            ++close_failed;
            ++failed;
            LOG_ERROR(TAG, "TCPDIAG CLOSE FAIL attempt=%lu err=%s resp=%s",
                      (unsigned long)attempts, esp_err_to_name(err), response);
        }

        ++full_snapshot_counter;
        if (full_snapshot_counter >= 5U) {
            full_snapshot_counter = 0U;
            (void)hardware_test_sim_query("REG", "AT+CEREG?", "+CEREG:");
            (void)hardware_test_sim_query("OPERATOR", "AT+COPS?", "+COPS:");
            (void)hardware_test_sim_query("PDP", "AT+CNACT?", "+CNACT:");
            (void)hardware_test_sim_query("RADIO", "AT+CPSI?", "+CPSI:");
        }

        LOG_INFO(TAG,
                 "TCPDIAG TOTAL attempts=%lu opened=%lu failed=%lu close_failed=%lu",
                 (unsigned long)attempts, (unsigned long)opened,
                 (unsigned long)failed, (unsigned long)close_failed);
        hardware_test_log_result("SIM_TCP", cycle_ok,
                                 cycle_ok ? "TCP conectado, estado confirmado y socket cerrado"
                                          : "revisar estado/cierre TCP de este intento");
    }

    LOG_INFO(TAG,
             "TCPDIAG FINAL attempts=%lu opened=%lu failed=%lu close_failed=%lu target_ip=%s",
             (unsigned long)attempts, (unsigned long)opened,
             (unsigned long)failed, (unsigned long)close_failed, tcp_target);
    hardware_test_log_result("SIM_TCP", opened > 0U,
                             (opened > 0U)
                                 ? "TCP por IP resuelta confirmado al menos una vez"
                                 : "TCP por IP resuelta no confirmado tras tres intentos");
    for (;;) {
        hardware_test_delay(60000U);
    }
}

static esp_err_t hardware_test_sim_mqtt_start(void)
{
    esp_err_t err = hardware_test_sim_basic();
    if (err == ESP_OK) {
        err = sim7080g_wait_network(METER_SIM7080G_NETWORK_TIMEOUT_MS);
    }
    if (err == ESP_OK) {
        err = sim7080g_activate_pdp();
    }
    if (err == ESP_OK) {
        err = mqtt_topics_init();
    }
    if (err == ESP_OK) {
        err = mqtt_client_init();
    }
    if (err == ESP_OK) {
        err = mqtt_client_connect();
    }
    return err;
}

static void hardware_test_sim_mqtt_log_diagnostics(const char *stage,
                                                   esp_err_t err)
{
    mqtt_diagnostics_t diag = {0};
    mqtt_client_get_diagnostics(&diag);
    LOG_WARN(TAG,
             "MQTT_ISO %s err=%s stage=%s CEREG=%d CNACT=%d SMSTATE=%d "
             "publish_err=%ld recovery_err=%ld recovery_ms=%lu",
             stage,
             esp_err_to_name(err),
             diag.recovery_stage,
             diag.cereg_stat,
             diag.cnact_state,
             diag.smstate,
             (long)diag.last_publish_error,
             (long)diag.last_recovery_error,
             (unsigned long)diag.last_recovery_ms);
}

// Misma politica de reintento que task_communication: exponencial hasta
// METER_COMM_RECOVERY_RETRY_MAX_MS y ritmo lento tras un corte prolongado.
static uint32_t hardware_test_sim_mqtt_backoff_ms(uint32_t failures,
                                                  int64_t outage_start_us)
{
    uint32_t shift = (failures > 0U) ? (failures - 1U) : 0U;
    if (shift > 4U) {
        shift = 4U;
    }
    uint32_t backoff_ms = METER_COMM_RECOVERY_RETRY_MS << shift;
    if (backoff_ms > METER_COMM_RECOVERY_RETRY_MAX_MS) {
        backoff_ms = METER_COMM_RECOVERY_RETRY_MAX_MS;
    }
    if ((esp_timer_get_time() - outage_start_us) >=
        ((int64_t)METER_COMM_OUTAGE_SLOW_AFTER_MS * 1000LL)) {
        backoff_ms = METER_COMM_OUTAGE_SLOW_RETRY_MS;
    }
    return backoff_ms;
}

static void hardware_test_run_sim_mqtt(void)
{
    char topic[96];
    LOG_INFO(TAG, "MQTT_ISO firmware=%s profile=test_sim_mqtt",
             METER_FW_VERSION);
    if (mqtt_topics_init() != ESP_OK) {
        hardware_test_log_result("MQTT_ISO", false, "no se pudo resolver el ID del dispositivo");
        for (;;) { hardware_test_delay(HW_TEST_RETRY_MS); }
    }

    const int topic_len = snprintf(topic, sizeof(topic), "medidor/%s/diagnostico",
                                   mqtt_topics_device_id());
    if ((topic_len <= 0) || ((size_t)topic_len >= sizeof(topic))) {
        hardware_test_log_result("MQTT_ISO", false, "topic de diagnostico demasiado largo");
        for (;;) { hardware_test_delay(HW_TEST_RETRY_MS); }
    }

    LOG_WARN(TAG, "MQTT aislado por SIM7080G; mantener AC desconectada");
    LOG_INFO(TAG, "MQTT_ISO topic=%s qos=1 retain=0 periodo=%lu ms",
             topic, (unsigned long)HW_TEST_SIM_MQTT_PUBLISH_PERIOD_MS);

    uint32_t session_attempts = 0U;
    uint32_t sessions_connected = 0U;
    uint32_t connection_drops = 0U;
    uint32_t recovery_attempts = 0U;
    uint32_t recovery_successes = 0U;
    uint32_t publish_attempts = 0U;
    uint32_t publish_successes = 0U;
    uint32_t publish_failures = 0U;
    uint32_t seq = 0U;
    bool had_session = false;
    int64_t next_publish_us = 0LL;
    uint32_t connect_failures = 0U;
    int64_t outage_start_us = 0LL;

    for (;;) {
        if (!had_session) {
            ++session_attempts;
            const int64_t connect_start_us = esp_timer_get_time();
            const esp_err_t err = hardware_test_sim_mqtt_start();
            const uint32_t connect_ms =
                (uint32_t)((esp_timer_get_time() - connect_start_us) / 1000LL);
            if (err != ESP_OK) {
                if (connect_failures == 0U) {
                    outage_start_us = connect_start_us;
                }
                ++connect_failures;
                const uint32_t backoff_ms =
                    hardware_test_sim_mqtt_backoff_ms(connect_failures, outage_start_us);
                hardware_test_sim_mqtt_log_diagnostics("CONNECT_FAIL", err);
                hardware_test_log_result("MQTT_ISO CONNECT", false,
                                         "fallo inicio modem/red o conexion al broker");
                LOG_INFO(TAG, "MQTT_ISO RETRY failures=%lu outage_s=%lu backoff_ms=%lu busy=%d",
                         (unsigned long)connect_failures,
                         (unsigned long)((esp_timer_get_time() - outage_start_us) / 1000000LL),
                         (unsigned long)backoff_ms,
                         (int)sim7080g_is_modem_busy());
                hardware_test_delay(backoff_ms);
                continue;
            }

            ++sessions_connected;
            had_session = true;
            next_publish_us = 0LL;
            LOG_INFO(TAG,
                     "MQTT_ISO CONNECT PASS attempt=%lu session=%lu elapsed_ms=%lu "
                     "smconn_ms=%lu SMSTATE=1 outage_s=%lu",
                     (unsigned long)session_attempts,
                     (unsigned long)sessions_connected,
                     (unsigned long)connect_ms,
                     (unsigned long)mqtt_client_get_last_connect_ms(),
                     (unsigned long)((connect_failures > 0U)
                         ? ((esp_timer_get_time() - outage_start_us) / 1000000LL)
                         : 0LL));
            connect_failures = 0U;
            hardware_test_log_result("MQTT_ISO CONNECT", true,
                                     "sesion con broker activa; ADE/SD excluidos");
        } else if (!mqtt_client_check_connected()) {
            ++connection_drops;
            hardware_test_sim_mqtt_log_diagnostics("SESSION_DROP", ESP_ERR_INVALID_STATE);
            hardware_test_log_result("MQTT_ISO SESSION", false,
                                     "SMSTATE no confirma sesion activa");
            ++recovery_attempts;
            ++session_attempts;
            const esp_err_t recovery_err = mqtt_client_recover();
            if (recovery_err == ESP_OK) {
                ++recovery_successes;
                ++sessions_connected;
                LOG_INFO(TAG, "MQTT_ISO RECOVERY PASS attempts=%lu successes=%lu",
                         (unsigned long)recovery_attempts,
                         (unsigned long)recovery_successes);
                had_session = true;
                next_publish_us = 0LL;
            } else {
                had_session = false;
                outage_start_us = esp_timer_get_time();
                connect_failures = 1U;
                hardware_test_sim_mqtt_log_diagnostics("RECOVERY_FAIL", recovery_err);
                hardware_test_log_result("MQTT_ISO RECOVERY", false,
                                         "no se restablecio la sesion MQTT");
                hardware_test_delay(HW_TEST_SIM_STATUS_PERIOD_MS);
                continue;
            }
        }

        const int64_t now_us = esp_timer_get_time();
        if (next_publish_us == 0LL || now_us >= next_publish_us) {
            ++seq;
            ++publish_attempts;
            char payload[160];
            const int payload_len = snprintf(payload, sizeof(payload),
                                             "{\"kind\":\"mqtt_isolated_probe\","
                                             "\"seq\":%lu,\"uptime_ms\":%llu}",
                                             (unsigned long)seq,
                                             (unsigned long long)(now_us / 1000LL));
            const int64_t publish_start_us = esp_timer_get_time();
            const esp_err_t publish_err = (payload_len > 0 &&
                                           (size_t)payload_len < sizeof(payload))
                ? mqtt_client_publish(topic, payload, 1, 0)
                : ESP_ERR_INVALID_SIZE;
            const uint32_t publish_ms =
                (uint32_t)((esp_timer_get_time() - publish_start_us) / 1000LL);

            if (publish_err == ESP_OK) {
                ++publish_successes;
                LOG_INFO(TAG,
                         "MQTT_ISO PUBLISH PASS seq=%lu qos=1 ms=%lu "
                         "sent=%lu/%lu SMSTATE=1",
                         (unsigned long)seq,
                         (unsigned long)publish_ms,
                         (unsigned long)publish_successes,
                         (unsigned long)publish_attempts);
                next_publish_us = esp_timer_get_time() +
                    ((int64_t)HW_TEST_SIM_MQTT_PUBLISH_PERIOD_MS * 1000LL);
            } else {
                ++publish_failures;
                LOG_WARN(TAG,
                         "MQTT_ISO PUBLISH FAIL seq=%lu qos=1 err=%s ms=%lu "
                         "sent=%lu/%lu failures=%lu",
                         (unsigned long)seq,
                         esp_err_to_name(publish_err),
                         (unsigned long)publish_ms,
                         (unsigned long)publish_successes,
                         (unsigned long)publish_attempts,
                         (unsigned long)publish_failures);
                hardware_test_sim_mqtt_log_diagnostics("PUBLISH_FAIL", publish_err);
                next_publish_us = esp_timer_get_time() +
                    ((int64_t)HW_TEST_SIM_STATUS_PERIOD_MS * 1000LL);
            }
            LOG_INFO(TAG,
                     "MQTT_ISO SUMMARY sessions=%lu/%lu drops=%lu recoveries=%lu/%lu "
                     "published=%lu/%lu publish_failures=%lu uptime_s=%llu",
                     (unsigned long)sessions_connected,
                     (unsigned long)session_attempts,
                     (unsigned long)connection_drops,
                     (unsigned long)recovery_successes,
                     (unsigned long)recovery_attempts,
                     (unsigned long)publish_successes,
                     (unsigned long)publish_attempts,
                     (unsigned long)publish_failures,
                     (unsigned long long)(esp_timer_get_time() / 1000000LL));
        }

        hardware_test_delay(HW_TEST_SIM_STATUS_PERIOD_MS);
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

/* q1-rc2: cabezas de cola que antes detenian la entrega en silencio.
 * Telemetria: linea vacia + JSON truncado + registro valido -> el servicio
 * debe retirar el truncado y reclamar el valido. Alertas: linea de 2100 B
 * (mayor que el buffer de 2048) -> la cola debe pasar a cuarentena .badN.
 * Solo toca METER_SD_BASE_DIR (codex_sd_test en este perfil). */
#define HW_TEST_HEAD_OK_PAYLOAD "[{\"n\":\"head_ok\",\"v\":1}]"
static bool s_queue_heads_seeded;

static void hardware_test_seed_queue_heads(void)
{
    (void)sd_storage_remove_file(METER_SD_TELEMETRY_QUEUE_FILE);
    (void)sd_storage_remove_file(METER_SD_ALERT_QUEUE_FILE);
    for (unsigned n = 1U; n <= 9U; ++n) {
        char bad[96];
        (void)snprintf(bad, sizeof(bad), "%s.bad%u", METER_SD_ALERT_QUEUE_FILE, n);
        (void)sd_storage_remove_file(bad);
    }
    static char long_line[2101];
    (void)memset(long_line, 'x', sizeof(long_line) - 1U);
    long_line[0] = '[';
    long_line[sizeof(long_line) - 2U] = ']';
    long_line[sizeof(long_line) - 1U] = '\0';
    s_queue_heads_seeded =
        (sd_storage_append_line(METER_SD_TELEMETRY_QUEUE_FILE, "") == ESP_OK) &&
        (sd_storage_append_line(METER_SD_TELEMETRY_QUEUE_FILE,
            "{\"v\":1,\"type\":\"telemetry_pending\",\"payload\":[{\"n\":\"cut\"") == ESP_OK) &&
        (sd_storage_append_line(METER_SD_TELEMETRY_QUEUE_FILE,
                                HW_TEST_HEAD_OK_PAYLOAD) == ESP_OK) &&
        (sd_storage_append_line(METER_SD_ALERT_QUEUE_FILE, long_line) == ESP_OK) &&
        (sd_storage_append_line(METER_SD_ALERT_QUEUE_FILE,
                                "[{\"n\":\"after_long\",\"v\":1}]") == ESP_OK);
}

static void hardware_test_check_queue_heads(void)
{
    storage_service_snapshot_t snapshot = {0};
    static char head[STORAGE_SERVICE_PAYLOAD_MAX];
    bool head_ok = false;
    const int64_t deadline = esp_timer_get_time() + 5000000LL;
    do {
        hardware_test_delay(100U);
        (void)storage_service_get_snapshot(&snapshot);
        head_ok = (storage_service_peek_pending(STORAGE_RECORD_TELEMETRY, head,
                                                sizeof(head)) == ESP_OK) &&
                  (strcmp(head, HW_TEST_HEAD_OK_PAYLOAD) == 0);
    } while ((!head_ok || (snapshot.queue_files_quarantined == 0U)) &&
             (esp_timer_get_time() < deadline));
    char bad1[96];
    (void)snprintf(bad1, sizeof(bad1), "%s.bad1", METER_SD_ALERT_QUEUE_FILE);
    uint64_t bad_size = 0U;
    const bool bad_exists = sd_storage_get_file_size(bad1, &bad_size) == ESP_OK;
    LOG_INFO(TAG, "SD_SERVICE HEAD seeded=%u head_ok=%u discarded=%lu quarantined=%lu bad1=%u (%llu B)",
             (unsigned)s_queue_heads_seeded, (unsigned)head_ok,
             (unsigned long)snapshot.queue_heads_discarded,
             (unsigned long)snapshot.queue_files_quarantined,
             (unsigned)bad_exists, (unsigned long long)bad_size);
    hardware_test_log_result("SD_SERVICE HEAD_DISCARD",
                             s_queue_heads_seeded && head_ok &&
                             (snapshot.queue_heads_discarded >= 1U),
                             "linea vacia y JSON truncado retirados; siguiente registro reclamado");
    hardware_test_log_result("SD_SERVICE HEAD_QUARANTINE",
                             s_queue_heads_seeded &&
                             (snapshot.queue_files_quarantined >= 1U) && bad_exists,
                             "cabeza > 2048 B: cola renombrada a .bad1 sin borrar datos");
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
        hardware_test_seed_queue_heads();
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
    hardware_test_check_queue_heads();

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
    /* Cada productor hace un xTaskNotifyGive. Con pdTRUE la primera espera
     * consumia ambas notificaciones y la segunda vencia: falso FAIL que se
     * repetia desde q1-rc1. Se acumulan hasta contar dos. */
    uint32_t producers_notified = 0U;
    const int64_t producers_deadline = esp_timer_get_time() + 10000000LL;
    while ((producers_notified < 2U) && (esp_timer_get_time() < producers_deadline)) {
        producers_notified += ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000U));
    }
    const bool producers_done = producers_notified >= 2U;
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
           (METER_HW_TEST_PROFILE == HW_TEST_PROFILE_SIM_MQTT) ||
           (METER_HW_TEST_PROFILE == HW_TEST_PROFILE_SIM_TCP) ||
           (METER_HW_TEST_PROFILE == HW_TEST_PROFILE_SIM_PWRKEY) ||
           (METER_HW_TEST_PROFILE == HW_TEST_PROFILE_OLED_RTC) ||
           (METER_HW_TEST_PROFILE == HW_TEST_PROFILE_OLED_RTC_SD) ||
           (METER_HW_TEST_PROFILE == HW_TEST_PROFILE_SYSTEM_CONTRACTS) ||
           (METER_HW_TEST_PROFILE == HW_TEST_PROFILE_RTC_SERVICE) ||
           (METER_HW_TEST_PROFILE == HW_TEST_PROFILE_OLED_RTC_SERVICE) ||
           (METER_HW_TEST_PROFILE == HW_TEST_PROFILE_SD_SERVICE) ||
           (METER_HW_TEST_PROFILE == HW_TEST_PROFILE_ADE_SMOKE) ||
           (METER_HW_TEST_PROFILE == HW_TEST_PROFILE_ADE_MEASUREMENT) ||
           (METER_HW_TEST_PROFILE == HW_TEST_PROFILE_ADE_FULL);
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
        case HW_TEST_PROFILE_SIM_MQTT:    return "test_sim_mqtt";
        case HW_TEST_PROFILE_SIM_TCP:     return "test_sim_tcp";
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
        case HW_TEST_PROFILE_ADE_SMOKE: return "test_ade_smoke";
        case HW_TEST_PROFILE_ADE_MEASUREMENT: return "test_ade_measurement";
        case HW_TEST_PROFILE_ADE_FULL: return "test_ade_full";
        default:                          return "invalid";
    }
}

void hardware_test_run(void)
{
    LOG_WARN(TAG, "============================================================");
    LOG_WARN(TAG, "HARDWARE TEST PROFILE: %s", hardware_test_profile_name());
    if ((METER_HW_TEST_PROFILE == HW_TEST_PROFILE_ADE_SMOKE) ||
        (METER_HW_TEST_PROFILE == HW_TEST_PROFILE_ADE_MEASUREMENT) ||
        (METER_HW_TEST_PROFILE == HW_TEST_PROFILE_ADE_FULL)) {
        LOG_WARN(TAG, "ADE9153A ACTIVO - INICIAR SIEMPRE CON LAS ENTRADAS AC DESCONECTADAS");
    } else {
        LOG_WARN(TAG, "ADE9153A EXCLUIDO - NO ENERGIZAR LA ZONA AC PARA ESTA PRUEBA");
    }
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
        case HW_TEST_PROFILE_SIM_MQTT:
            hardware_test_run_sim_mqtt();
            break;
        case HW_TEST_PROFILE_SIM_TCP:
            hardware_test_run_sim_tcp();
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
        case HW_TEST_PROFILE_ADE_SMOKE:
            hardware_test_run_ade_smoke();
            break;
        case HW_TEST_PROFILE_ADE_MEASUREMENT:
            hardware_test_run_ade_measurement();
            break;
        case HW_TEST_PROFILE_ADE_FULL:
            hardware_test_run_ade_full();
            break;
        default:
            hardware_test_log_result("PROFILE", false,
                                     "perfil standalone invalido");
            for (;;) {
                hardware_test_delay(HW_TEST_RETRY_MS);
            }
    }
}
