#include "ds3231.h"

#include <string.h>
#include "driver/i2c.h"
#include "board_config.h"
#include "logger.h"

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

static bool s_ready;

static uint8_t bcd_to_bin(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) & 0x0F) * 10 + (bcd & 0x0F));
}

static uint8_t bin_to_bcd(uint8_t bin)
{
    return (uint8_t)(((bin / 10) << 4) | (bin % 10));
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
        err = i2c_master_cmd_begin(OLED_I2C_PORT, cmd,
                                   pdMS_TO_TICKS(DS3231_I2C_TIMEOUT_MS));
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
        err = i2c_master_cmd_begin(OLED_I2C_PORT, cmd,
                                   pdMS_TO_TICKS(DS3231_I2C_TIMEOUT_MS));
    }
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t ds3231_ensure_i2c_driver(void)
{
    // OLED instala el driver en I2C_NUM_0. Si DS3231 arranca antes que
    // OLED (boot temprano), lo instalamos nosotros. Si OLED ya lo instalo,
    // i2c_driver_install devuelve ESP_ERR_INVALID_STATE (no es un error
    // para nuestro caso).
    const i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = OLED_SDA,
        .scl_io_num = OLED_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = OLED_I2C_CLK_HZ,
        .clk_flags = 0,
    };
    esp_err_t err = i2c_param_config(OLED_I2C_PORT, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_driver_install(OLED_I2C_PORT, cfg.mode, 0, 0, 0);
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        return err;
    }
    return ESP_OK;
}

esp_err_t ds3231_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    esp_err_t err = ds3231_ensure_i2c_driver();
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

    // Comprobar y limpiar OSF (Oscillator Stopped Flag). Si estaba set,
    // la hora no es confiable; se limpia pero se reporta para que el
    // backend/UI muestre "RTC perdio tiempo, sincroniza con NTP".
    uint8_t status = 0U;
    if (ds3231_read_regs(DS3231_REG_STATUS, &status, 1U) == ESP_OK) {
        if ((status & DS3231_STATUS_OSF_BIT) != 0U) {
            LOG_WARN(TAG, "OSF activo: RTC perdio tiempo, requiere re-sync");
            status &= (uint8_t)~DS3231_STATUS_OSF_BIT;
            (void)ds3231_write_regs(DS3231_REG_STATUS, &status, 1U);
        }
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
        return err;
    }

    memset(out_tm, 0, sizeof(*out_tm));
    out_tm->tm_sec  = (int)bcd_to_bin(raw[0] & 0x7F);
    out_tm->tm_min  = (int)bcd_to_bin(raw[1] & 0x7F);
    // Bit 6 del reg HOURS: 1 = modo 12h, 0 = modo 24h. Asumimos 24h.
    out_tm->tm_hour = (int)bcd_to_bin(raw[2] & 0x3F);
    // raw[3] = day of week (1..7), no lo usamos: mktime lo recalcula.
    out_tm->tm_mday = (int)bcd_to_bin(raw[4] & 0x3F);
    const uint8_t month_raw = raw[5] & 0x1F;
    out_tm->tm_mon  = (int)bcd_to_bin(month_raw) - 1;  // tm: 0..11
    // tm_year es anos desde 1900. DS3231 cubre 2000..2099.
    const uint8_t century = (raw[5] & 0x80) ? 1U : 0U;
    out_tm->tm_year = (int)bcd_to_bin(raw[6]) + 100 + (century * 100);
    return ESP_OK;
}

esp_err_t ds3231_set_time(const struct tm *tm)
{
    if (tm == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t raw[7];
    raw[0] = bin_to_bcd((uint8_t)tm->tm_sec)  & 0x7F;
    raw[1] = bin_to_bcd((uint8_t)tm->tm_min)  & 0x7F;
    raw[2] = bin_to_bcd((uint8_t)tm->tm_hour) & 0x3F;  // modo 24h
    raw[3] = 1U;  // DoW no lo usamos, escribimos 1 valido.
    raw[4] = bin_to_bcd((uint8_t)tm->tm_mday) & 0x3F;
    uint8_t month_bcd = bin_to_bcd((uint8_t)(tm->tm_mon + 1)) & 0x1F;
    const uint8_t century_bit = (tm->tm_year >= 200) ? 0x80U : 0x00U;
    raw[5] = (uint8_t)(month_bcd | century_bit);
    const int year_in_century = (tm->tm_year - 100) % 100;
    raw[6] = bin_to_bcd((uint8_t)year_in_century);

    return ds3231_write_regs(DS3231_REG_SECONDS, raw, sizeof(raw));
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
    // DS3231 da UTC (asumiendo que set_time se hizo con UTC).
    // timegm no es portable: usamos mktime + offset. Como ESP-IDF
    // por defecto corre en UTC (TZ=UTC), mktime funciona aqui.
    time_t epoch = mktime(&t);
    if (epoch == (time_t)-1) {
        return ESP_FAIL;
    }
    *out_epoch = epoch;
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
        return err;
    }
    // MSB: signed integer. LSB: bits 7:6 = fraccion en cuartos (0.25 C).
    const int8_t msb = (int8_t)raw[0];
    const uint8_t frac = (raw[1] >> 6) & 0x03;
    *out_c = (float)msb + (0.25f * (float)frac);
    return ESP_OK;
}
