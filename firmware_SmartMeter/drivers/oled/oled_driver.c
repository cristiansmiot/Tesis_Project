#include "oled_driver.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "board_config.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger.h"

typedef struct {
    char ch;
    const char *rows[5];
} oled_glyph_t;

static const char *TAG = "oled_driver";
static bool s_i2c_ready;
static bool s_display_ready;
static uint8_t s_framebuffer[OLED_WIDTH * (OLED_HEIGHT / 8U)];

static const oled_glyph_t k_glyphs[] = {
    {' ', {"...", "...", "...", "...", "..."}},
    {'-', {"...", "...", "###", "...", "..."}},
    {'.', {"...", "...", "...", "...", ".#."}},
    {':', {"...", ".#.", "...", ".#.", "..."}},
    {'/', {"..#", "..#", ".#.", "#..", "#.."}},
    {'>', {"#..", ".#.", "..#", ".#.", "#.."}},
    {'?', {"###", "..#", ".#.", "...", ".#."}},
    {'0', {"###", "#.#", "#.#", "#.#", "###"}},
    {'1', {".#.", "##.", ".#.", ".#.", "###"}},
    {'2', {"###", "..#", "###", "#..", "###"}},
    {'3', {"###", "..#", "###", "..#", "###"}},
    {'4', {"#.#", "#.#", "###", "..#", "..#"}},
    {'5', {"###", "#..", "###", "..#", "###"}},
    {'6', {"###", "#..", "###", "#.#", "###"}},
    {'7', {"###", "..#", "..#", ".#.", ".#."}},
    {'8', {"###", "#.#", "###", "#.#", "###"}},
    {'9', {"###", "#.#", "###", "..#", "###"}},
    {'A', {".#.", "#.#", "###", "#.#", "#.#"}},
    {'B', {"##.", "#.#", "##.", "#.#", "##."}},
    {'C', {".##", "#..", "#..", "#..", ".##"}},
    {'D', {"##.", "#.#", "#.#", "#.#", "##."}},
    {'E', {"###", "#..", "##.", "#..", "###"}},
    {'F', {"###", "#..", "##.", "#..", "#.."}},
    {'G', {".##", "#..", "#.#", "#.#", ".##"}},
    {'H', {"#.#", "#.#", "###", "#.#", "#.#"}},
    {'I', {"###", ".#.", ".#.", ".#.", "###"}},
    {'J', {"..#", "..#", "..#", "#.#", "###"}},
    {'K', {"#.#", "#.#", "##.", "#.#", "#.#"}},
    {'L', {"#..", "#..", "#..", "#..", "###"}},
    {'M', {"#.#", "###", "###", "#.#", "#.#"}},
    {'N', {"#.#", "###", "###", "###", "#.#"}},
    {'O', {"###", "#.#", "#.#", "#.#", "###"}},
    {'P', {"###", "#.#", "###", "#..", "#.."}},
    {'Q', {"###", "#.#", "#.#", "###", "..#"}},
    {'R', {"##.", "#.#", "##.", "#.#", "#.#"}},
    {'S', {".##", "#..", "###", "..#", "##."}},
    {'T', {"###", ".#.", ".#.", ".#.", ".#."}},
    {'U', {"#.#", "#.#", "#.#", "#.#", "###"}},
    {'V', {"#.#", "#.#", "#.#", "#.#", ".#."}},
    {'W', {"#.#", "#.#", "###", "###", "#.#"}},
    {'X', {"#.#", "#.#", ".#.", "#.#", "#.#"}},
    {'Y', {"#.#", "#.#", ".#.", ".#.", ".#."}},
    {'Z', {"###", "..#", ".#.", "#..", "###"}},
};

static const oled_glyph_t *oled_driver_find_glyph(char ch)
{
    char upper = ch;
    if ((upper >= 'a') && (upper <= 'z')) {
        upper = (char)(upper - ('a' - 'A'));
    }

    for (size_t i = 0U; i < (sizeof(k_glyphs) / sizeof(k_glyphs[0])); ++i) {
        if (k_glyphs[i].ch == upper) {
            return &k_glyphs[i];
        }
    }

    for (size_t i = 0U; i < (sizeof(k_glyphs) / sizeof(k_glyphs[0])); ++i) {
        if (k_glyphs[i].ch == '?') {
            return &k_glyphs[i];
        }
    }
    return NULL;
}

static esp_err_t oled_driver_write_block(uint8_t control, const uint8_t *data, size_t len)
{
    if ((data == NULL) || (len == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = i2c_master_start(cmd);
    if (err == ESP_OK) {
        err = i2c_master_write_byte(cmd, (uint8_t)((OLED_I2C_ADDR << 1U) | I2C_MASTER_WRITE), true);
    }
    if (err == ESP_OK) {
        err = i2c_master_write_byte(cmd, control, true);
    }
    if (err == ESP_OK) {
        err = i2c_master_write(cmd, (uint8_t *)data, len, true);
    }
    if (err == ESP_OK) {
        err = i2c_master_stop(cmd);
    }
    if (err == ESP_OK) {
        err = i2c_master_cmd_begin(OLED_I2C_PORT, cmd, pdMS_TO_TICKS(OLED_I2C_TIMEOUT_MS));
    }
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t oled_driver_write_commands(const uint8_t *cmds, size_t count)
{
    return oled_driver_write_block(0x00U, cmds, count);
}

static esp_err_t oled_driver_write_data_chunk(const uint8_t *data, size_t count)
{
    return oled_driver_write_block(0x40U, data, count);
}

esp_err_t oled_driver_init(void)
{
    if (!s_i2c_ready) {
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
            LOG_WARN(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
            return err;
        }
        err = i2c_driver_install(OLED_I2C_PORT, cfg.mode, 0, 0, 0);
        if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
            LOG_WARN(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
            return err;
        }
        s_i2c_ready = true;
    }

    const uint8_t init_cmds[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
        0x2E, 0xAF,
    };
    esp_err_t err = oled_driver_write_commands(init_cmds, sizeof(init_cmds));
    if (err != ESP_OK) {
        LOG_WARN(TAG, "OLED init commands failed: %s", esp_err_to_name(err));
        s_display_ready = false;
        return err;
    }

    (void)memset(s_framebuffer, 0, sizeof(s_framebuffer));
    s_display_ready = true;
    return oled_driver_flush();
}

esp_err_t oled_driver_clear(void)
{
    if (!s_display_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)memset(s_framebuffer, 0, sizeof(s_framebuffer));
    return ESP_OK;
}

esp_err_t oled_driver_draw_text(uint8_t row, const char *text)
{
    if (!s_display_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((text == NULL) || (row >= OLED_DRIVER_MAX_ROWS)) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t page_offset = (size_t)row * OLED_WIDTH;
    (void)memset(&s_framebuffer[page_offset], 0, OLED_WIDTH);

    size_t col = 0U;
    for (size_t idx = 0U; (text[idx] != '\0') && (col < (OLED_WIDTH - 3U)); ++idx) {
        const oled_glyph_t *glyph = oled_driver_find_glyph(text[idx]);
        if (glyph == NULL) {
            continue;
        }

        for (size_t glyph_col = 0U; glyph_col < 3U; ++glyph_col) {
            uint8_t bits = 0U;
            for (size_t glyph_row = 0U; glyph_row < 5U; ++glyph_row) {
                if (glyph->rows[glyph_row][glyph_col] == '#') {
                    bits = (uint8_t)(bits | (1U << glyph_row));
                }
            }
            s_framebuffer[page_offset + col + glyph_col] = bits;
        }
        col += 4U;
    }

    return ESP_OK;
}

esp_err_t oled_driver_flush(void)
{
    if (!s_display_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t addr_cmds[] = {0x21, 0x00, (uint8_t)(OLED_WIDTH - 1U), 0x22, 0x00, (uint8_t)((OLED_HEIGHT / 8U) - 1U)};
    esp_err_t err = oled_driver_write_commands(addr_cmds, sizeof(addr_cmds));
    if (err != ESP_OK) {
        /* Retry once before declaring display dead. */
        vTaskDelay(pdMS_TO_TICKS(5));
        err = oled_driver_write_commands(addr_cmds, sizeof(addr_cmds));
        if (err != ESP_OK) {
            s_display_ready = false;
            return err;
        }
    }

    for (size_t offset = 0U; offset < sizeof(s_framebuffer); offset += 16U) {
        const size_t chunk = ((sizeof(s_framebuffer) - offset) < 16U) ? (sizeof(s_framebuffer) - offset) : 16U;
        err = oled_driver_write_data_chunk(&s_framebuffer[offset], chunk);
        if (err != ESP_OK) {
            /* Retry the failed chunk once. */
            vTaskDelay(pdMS_TO_TICKS(2));
            err = oled_driver_write_data_chunk(&s_framebuffer[offset], chunk);
            if (err != ESP_OK) {
                s_display_ready = false;
                return err;
            }
        }
    }

    return ESP_OK;
}
