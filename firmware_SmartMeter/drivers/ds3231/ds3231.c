#include "ds3231.h"

#include <string.h>
#include "driver/i2c.h"
#include "board_config.h"
#include "i2c_shared.h"
#include "logger.h"
#include "system_contracts.h"

static const char *TAG = "ds3231";

// Registros DS3231 (datasheet Maxim/Analog)
#define DS3231_REG_SECONDS      0x00
#define DS3231_REG_MINUTES      0x01
#define DS3231_REG_HOURS        0x02
#define DS3231_REG_DAY          0x03  // 1..7
#define DS3231_REG_DATE         0x04  // 1..31
#define DS3231_REG_MONTH        0x05  // bit 7 = century
#define DS3231_REG_YEAR         0x06  // 0..99
#define DS3231_REG_CONTROL      0x0E
#define DS3231_REG_STATUS       0x0F
#define DS3231_REG_TEMP_MSB     0x11
#define DS3231_REG_TEMP_LSB     0x12

#define DS3231_STATUS_OSF_BIT   0x80  // Oscillator Stop Flag
#define DS3231_CONTROL_EOSC_BIT 0x80  // 1 detiene oscilador al usar VBAT

static bool s_ready;
static bool s_oscillator_stopped;
static bool s_eosc_set;

static uint8_t bcd_to_bin(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) & 0x0F) * 10 + (bcd & 0x0F));
}

static uint8_t bin_to_bcd(uint8_t bin)
{
    return (uint8_t)(((bin / 10) << 4) | (bin % 10));
}

static bool bcd_valid(uint8_t value)
{
    return ((value & 0x0FU) <= 9U) && (((value >> 4U) & 0x0FU) <= 9U);
}

static bool is_leap_year(int year)
{
    return ((year % 4) == 0) && (((year % 100) != 0) || ((year % 400) == 0));
}

static bool tm_fields_valid(const struct tm *tm, int min_year)
{
    if (tm == NULL) {
        return false;
    }
    const int year = tm->tm_year + 1900;
    const int month = tm->tm_mon + 1;
    if ((year < min_year) || (year > 2099) || (month < 1) || (month > 12) ||
        (tm->tm_hour < 0) || (tm->tm_hour > 23) ||
        (tm->tm_min < 0) || (tm->tm_min > 59) ||
        (tm->tm_sec < 0) || (tm->tm_sec > 59)) {
        return false;
    }
    static const uint8_t days_per_month[] = {
        31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U
    };
    int max_day = days_per_month[month - 1];
    if ((month == 2) && is_leap_year(year)) {
        max_day = 29;
    }
    return (tm->tm_mday >= 1) && (tm->tm_mday <= max_day);
}

/* Dias desde 1970-01-01. Algoritmo civil, independiente de TZ/mktime(). */
static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= (month <= 2U) ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = (unsigned)(year - era * 400);
    const unsigned shifted_month = (month > 2U) ? (month - 3U) : (month + 9U);
    const unsigned day_of_year = (153U * shifted_month + 2U) / 5U + day - 1U;
    const unsigned day_of_era = year_of_era * 365U + year_of_era / 4U -
                                year_of_era / 100U + day_of_year;
    return (int64_t)era * 146097LL + (int64_t)day_of_era - 719468LL;
}

esp_err_t ds3231_tm_to_epoch_utc(const struct tm *tm, time_t *out_epoch)
{
    if ((tm == NULL) || (out_epoch == NULL) || !tm_fields_valid(tm, 2000)) {
        return ESP_ERR_INVALID_ARG;
    }
    const int64_t days = days_from_civil(tm->tm_year + 1900,
                                         (unsigned)(tm->tm_mon + 1),
                                         (unsigned)tm->tm_mday);
    const int64_t seconds = days * 86400LL + (int64_t)tm->tm_hour * 3600LL +
                            (int64_t)tm->tm_min * 60LL + (int64_t)tm->tm_sec;
    *out_epoch = (time_t)seconds;
    return ESP_OK;
}

// Lee N bytes consecutivos empezando en reg.
static esp_err_t ds3231_read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    if ((buf == NULL) || (len == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = i2c_master_start(cmd);
    if (err == ESP_OK) {
        err = i2c_master_write_byte(cmd,
            (uint8_t)((DS3231_I2C_ADDR << 1U) | I2C_MASTER_WRITE), true);
    }
    if (err == ESP_OK) {
        err = i2c_master_write_byte(cmd, reg, true);
    }
    if (err == ESP_OK) {
        err = i2c_master_start(cmd);  // repeated start
    }
    if (err == ESP_OK) {
        err = i2c_master_write_byte(cmd,
            (uint8_t)((DS3231_I2C_ADDR << 1U) | I2C_MASTER_READ), true);
    }
    if (err == ESP_OK) {
        if (len > 1U) {
            err = i2c_master_read(cmd, buf, len - 1U, I2C_MASTER_ACK);
        }
    }
    if (err == ESP_OK) {
        err = i2c_master_read_byte(cmd, &buf[len - 1U], I2C_MASTER_NACK);
    }
    if (err == ESP_OK) {
        err = i2c_master_stop(cmd);
    }
    if (err == ESP_OK) {
        err = i2c_shared_lock(pdMS_TO_TICKS(DS3231_I2C_TIMEOUT_MS));
        if (err == ESP_OK) {
            err = i2c_master_cmd_begin(OLED_I2C_PORT, cmd,
                                       pdMS_TO_TICKS(DS3231_I2C_TIMEOUT_MS));
            i2c_shared_unlock();
        }
    }
    i2c_cmd_link_delete(cmd);
    return err;
}

// Escribe N bytes consecutivos empezando en reg.
static esp_err_t ds3231_write_regs(uint8_t reg, const uint8_t *buf, size_t len)
{
    if ((buf == NULL) || (len == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = i2c_master_start(cmd);
    if (err == ESP_OK) {
        err = i2c_master_write_byte(cmd,
            (uint8_t)((DS3231_I2C_ADDR << 1U) | I2C_MASTER_WRITE), true);
    }
    if (err == ESP_OK) {
        err = i2c_master_write_byte(cmd, reg, true);
    }
    if (err == ESP_OK) {
        err = i2c_master_write(cmd, (uint8_t *)buf, len, true);
    }
    if (err == ESP_OK) {
        err = i2c_master_stop(cmd);
    }
    if (err == ESP_OK) {
        err = i2c_shared_lock(pdMS_TO_TICKS(DS3231_I2C_TIMEOUT_MS));
        if (err == ESP_OK) {
            err = i2c_master_cmd_begin(OLED_I2C_PORT, cmd,
                                       pdMS_TO_TICKS(DS3231_I2C_TIMEOUT_MS));
            i2c_shared_unlock();
        }
    }
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t ds3231_ensure_oscillator_enabled(void)
{
    uint8_t control = 0U;
    esp_err_t err = ds3231_read_regs(DS3231_REG_CONTROL, &control, 1U);
    if (err != ESP_OK) {
        return err;
    }

    s_eosc_set = (control & DS3231_CONTROL_EOSC_BIT) != 0U;
    if (!s_eosc_set) {
        return ESP_OK;
    }

    LOG_WARN(TAG, "EOSC estaba activo; habilitando respaldo del oscilador");
    control &= (uint8_t)~DS3231_CONTROL_EOSC_BIT;
    err = ds3231_write_regs(DS3231_REG_CONTROL, &control, 1U);
    if (err != ESP_OK) {
        return err;
    }
    err = ds3231_read_regs(DS3231_REG_CONTROL, &control, 1U);
    if (err != ESP_OK) {
        return err;
    }
    s_eosc_set = (control & DS3231_CONTROL_EOSC_BIT) != 0U;
    return s_eosc_set ? ESP_ERR_INVALID_RESPONSE : ESP_OK;
}

esp_err_t ds3231_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    esp_err_t err = i2c_shared_init();
    if (err != ESP_OK) {
        LOG_WARN(TAG, "i2c driver install failed: %s", esp_err_to_name(err));
        return err;
    }

    // Ping: leer 1 byte del registro de segundos.
    uint8_t probe = 0U;
    err = ds3231_read_regs(DS3231_REG_SECONDS, &probe, 1U);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "DS3231 no responde en 0x%02X: %s",
                 DS3231_I2C_ADDR, esp_err_to_name(err));
        return err;
    }

    err = ds3231_ensure_oscillator_enabled();
    if (err != ESP_OK) {
        LOG_WARN(TAG, "no se pudo habilitar oscilador en VBAT: %s",
                 esp_err_to_name(err));
        return err;
    }

    // OSF se conserva hasta escribir y verificar una hora valida. Limpiarlo
    // durante init ocultaria una perdida de bateria/oscilador.
    uint8_t status = 0U;
    err = ds3231_read_regs(DS3231_REG_STATUS, &status, 1U);
    if (err != ESP_OK) {
        return err;
    }
    s_oscillator_stopped = (status & DS3231_STATUS_OSF_BIT) != 0U;
    if (s_oscillator_stopped) {
        LOG_WARN(TAG, "OSF activo: RTC perdio tiempo, requiere sincronizacion");
    }

    s_ready = true;
    LOG_INFO(TAG, "DS3231 OK @0x%02X (I2C compartido con OLED)",
             DS3231_I2C_ADDR);
    return ESP_OK;
}

bool ds3231_is_ready(void)
{
    return s_ready;
}

void ds3231_invalidate(void)
{
    s_ready = false;
    s_eosc_set = false;
}

esp_err_t ds3231_get_time(struct tm *out_tm)
{
    if (out_tm == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t raw[7] = {0};
    esp_err_t err = ds3231_read_regs(DS3231_REG_SECONDS, raw, sizeof(raw));
    if (err != ESP_OK) {
        s_ready = false;
        return err;
    }

    if (!bcd_valid(raw[0] & 0x7FU) || !bcd_valid(raw[1] & 0x7FU) ||
        !bcd_valid(raw[4] & 0x3FU) || !bcd_valid(raw[5] & 0x1FU) ||
        !bcd_valid(raw[6]) || ((raw[5] & 0x80U) != 0U)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    memset(out_tm, 0, sizeof(*out_tm));
    out_tm->tm_sec  = (int)bcd_to_bin(raw[0] & 0x7F);
    out_tm->tm_min  = (int)bcd_to_bin(raw[1] & 0x7F);
    if ((raw[2] & 0x40U) != 0U) {
        const uint8_t hour_bcd = raw[2] & 0x1FU;
        if (!bcd_valid(hour_bcd)) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        int hour = (int)bcd_to_bin(hour_bcd);
        if ((hour < 1) || (hour > 12)) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        hour %= 12;
        if ((raw[2] & 0x20U) != 0U) {
            hour += 12;
        }
        out_tm->tm_hour = hour;
    } else {
        const uint8_t hour_bcd = raw[2] & 0x3FU;
        if (!bcd_valid(hour_bcd)) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        out_tm->tm_hour = (int)bcd_to_bin(hour_bcd);
    }
    out_tm->tm_mday = (int)bcd_to_bin(raw[4] & 0x3F);
    const uint8_t month_raw = raw[5] & 0x1F;
    out_tm->tm_mon  = (int)bcd_to_bin(month_raw) - 1;  // tm: 0..11
    out_tm->tm_year = (int)bcd_to_bin(raw[6]) + 100;
    if (!tm_fields_valid(out_tm, 2000)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t ds3231_set_time(const struct tm *tm)
{
    if ((tm == NULL) || !tm_fields_valid(tm, 2020)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ds3231_ensure_oscillator_enabled();
    if (err != ESP_OK) {
        s_ready = false;
        return err;
    }

    time_t requested_epoch = 0;
    if (ds3231_tm_to_epoch_utc(tm, &requested_epoch) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    const int64_t days = (int64_t)requested_epoch / 86400LL;
    uint8_t raw[7];
    raw[0] = bin_to_bcd((uint8_t)tm->tm_sec)  & 0x7F;
    raw[1] = bin_to_bcd((uint8_t)tm->tm_min)  & 0x7F;
    raw[2] = bin_to_bcd((uint8_t)tm->tm_hour) & 0x3F;  // modo 24h
    raw[3] = (uint8_t)(((days + 4LL) % 7LL) + 1LL); // 1=domingo
    raw[4] = bin_to_bcd((uint8_t)tm->tm_mday) & 0x3F;
    uint8_t month_bcd = bin_to_bcd((uint8_t)(tm->tm_mon + 1)) & 0x1F;
    raw[5] = month_bcd;
    const int year_in_century = tm->tm_year - 100;
    raw[6] = bin_to_bcd((uint8_t)year_in_century);

    err = ds3231_write_regs(DS3231_REG_SECONDS, raw, sizeof(raw));
    if (err != ESP_OK) {
        s_ready = false;
        return err;
    }

    struct tm verify = {0};
    time_t verify_epoch = 0;
    err = ds3231_get_time(&verify);
    if ((err != ESP_OK) ||
        (ds3231_tm_to_epoch_utc(&verify, &verify_epoch) != ESP_OK) ||
        (verify_epoch < requested_epoch) ||
        ((verify_epoch - requested_epoch) > 1)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t status = 0U;
    err = ds3231_read_regs(DS3231_REG_STATUS, &status, 1U);
    if (err == ESP_OK) {
        status &= (uint8_t)~DS3231_STATUS_OSF_BIT;
        err = ds3231_write_regs(DS3231_REG_STATUS, &status, 1U);
    }
    if (err != ESP_OK) {
        s_ready = false;
        return err;
    }
    s_oscillator_stopped = false;
    return ESP_OK;
}

esp_err_t ds3231_get_epoch(time_t *out_epoch)
{
    if (out_epoch == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    struct tm t;
    esp_err_t err = ds3231_get_time(&t);
    if (err != ESP_OK) {
        return err;
    }
    return ds3231_tm_to_epoch_utc(&t, out_epoch);
}

esp_err_t ds3231_get_status(ds3231_status_t *out_status)
{
    if (out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)memset(out_status, 0, sizeof(*out_status));
    out_status->ready = s_ready;
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t control = 0U;
    esp_err_t err = ds3231_read_regs(DS3231_REG_CONTROL, &control, 1U);
    if (err != ESP_OK) {
        s_ready = false;
        out_status->ready = false;
        return err;
    }
    s_eosc_set = (control & DS3231_CONTROL_EOSC_BIT) != 0U;
    out_status->eosc_set = s_eosc_set;

    uint8_t status = 0U;
    err = ds3231_read_regs(DS3231_REG_STATUS, &status, 1U);
    if (err != ESP_OK) {
        s_ready = false;
        out_status->ready = false;
        return err;
    }
    s_oscillator_stopped = (status & DS3231_STATUS_OSF_BIT) != 0U;
    out_status->oscillator_stopped = s_oscillator_stopped;
    err = ds3231_get_epoch(&out_status->epoch_utc);
    if (err != ESP_OK) {
        return err;
    }
    out_status->time_valid = !s_oscillator_stopped && !s_eosc_set &&
        ((int64_t)out_status->epoch_utc >= SYSTEM_TIME_VALID_MIN_EPOCH) &&
        ((int64_t)out_status->epoch_utc <= SYSTEM_TIME_VALID_MAX_EPOCH);
    return ESP_OK;
}

esp_err_t ds3231_get_temperature(float *out_c)
{
    if (out_c == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t raw[2] = {0};
    esp_err_t err = ds3231_read_regs(DS3231_REG_TEMP_MSB, raw, sizeof(raw));
    if (err != ESP_OK) {
        s_ready = false;
        return err;
    }
    // MSB: signed integer. LSB: bits 7:6 = fraccion en cuartos (0.25 C).
    const int8_t msb = (int8_t)raw[0];
    const uint8_t frac = (raw[1] >> 6) & 0x03;
    *out_c = (float)msb + (0.25f * (float)frac);
    return ESP_OK;
}
