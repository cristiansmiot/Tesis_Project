#include "display_manager.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "menu_handler.h"
#include "meter_config.h"
#include "oled_driver.h"

static bool s_initialized;

static esp_err_t display_manager_write_line(uint8_t row, const char *fmt, ...)
{
    char line[OLED_DRIVER_MAX_COLS + 1U] = {0};
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    return oled_driver_draw_text(row, line);
}

static const char *display_manager_action_name(ade9153a_recovery_action_t action)
{
    switch (action) {
        case ADE9153A_RECOVERY_ACTION_PROBE:
            return "PROBE";
        case ADE9153A_RECOVERY_ACTION_SOFT_INIT:
            return "INIT";
        case ADE9153A_RECOVERY_ACTION_SPI_REINIT:
            return "SPI";
        default:
            return "-";
    }
}

static const char *display_manager_temp_text(const MeterData_t *meter, char *out, size_t out_len)
{
    if ((out == NULL) || (out_len == 0U)) {
        return "N/A";
    }
    if ((meter == NULL) || (meter->temperature_valid == 0U)) {
        (void)snprintf(out, out_len, "N/A");
        return out;
    }
    (void)snprintf(out, out_len, "%.1fC", (double)meter->temperature);
    return out;
}

static const char *display_manager_ac_badge(const ui_display_context_t *ctx)
{
    if ((ctx == NULL) || !ctx->ade.ready || ctx->ade.bus_disconnected) {
        return "ERR";
    }
    if (ctx->meter.ac_present == 0U) {
        return "OFF";
    }
    if (ctx->meter.active_power_w > METER_NO_LOAD_ACTIVE_POWER_W_MAX) {
        return "CAR";
    }
    return "OK";
}

static unsigned long display_manager_count_2d(uint32_t value)
{
    return (unsigned long)((value > 99U) ? 99U : value);
}

static const char *display_manager_ade_status(const ui_display_context_t *ctx)
{
    if ((ctx == NULL) || !ctx->ade.ready || ctx->ade.bus_disconnected) {
        return "ER";
    }
    return "OK";
}

static const char *display_manager_bus_status(const ui_display_context_t *ctx)
{
    if ((ctx == NULL) || !ctx->ade.ready) {
        return "ERR";
    }
    return ctx->ade.bus_disconnected ? "LOST" : "OK";
}

static esp_err_t display_manager_render_home(const ui_display_context_t *ctx)
{
    (void)display_manager_write_line(0U, "");
    (void)display_manager_write_line(1U, "  MEDIDOR INTELIGENTE");
    (void)display_manager_write_line(2U, "");
    (void)display_manager_write_line(3U, " CONSUMO: %.2f WH", (double)ctx->meter.energy_wh);
    (void)display_manager_write_line(4U, "");
    const char *ic_cel = ctx->cellular_connected ? "CEL:OK" : "CEL:--";
    const char *ic_mqt = ctx->mqtt_connected ? "MQT:OK" : "MQT:--";
    const char *ic_ade = (ctx->ade.ready && !ctx->ade.bus_disconnected) ? "ADE:OK" : "ADE:ER";
    (void)display_manager_write_line(5U, " %s %s %s", ic_cel, ic_mqt, ic_ade);
    (void)display_manager_write_line(6U, "");
    if (ctx->date_str[0] != '\0') {
        (void)display_manager_write_line(7U, "  %s", ctx->date_str);
    } else {
        (void)display_manager_write_line(7U, "  -- SIN FECHA --");
    }
    return ESP_OK;
}

static esp_err_t display_manager_render_root(const ui_menu_state_t *state)
{
    (void)display_manager_write_line(0U, "     SMART METER");
    (void)display_manager_write_line(1U, "----------------");
    (void)display_manager_write_line(2U, "%c MEDICIONES", (state->root_index == 0U) ? '>' : ' ');
    (void)display_manager_write_line(3U, "%c ENERGIA", (state->root_index == 1U) ? '>' : ' ');
    (void)display_manager_write_line(4U, "%c ESTADO NODOS", (state->root_index == 2U) ? '>' : ' ');
    (void)display_manager_write_line(5U, "%c DIAGNOSTICO", (state->root_index == 3U) ? '>' : ' ');
    (void)display_manager_write_line(6U, "%c ACCIONES", (state->root_index == 4U) ? '>' : ' ');
    return display_manager_write_line(7U, "SEL:ABRIR  BACK:--");
}

static esp_err_t display_manager_render_overview(const ui_display_context_t *ctx)
{
    (void)display_manager_write_line(0U, "      MEDICIONES");
    (void)display_manager_write_line(1U, "----------------");
    (void)display_manager_write_line(2U, "VOLTAJE  %.1f V", (double)ctx->meter.vrms);
    (void)display_manager_write_line(3U, "CORRIENT %.3f A", (double)ctx->meter.irms_a);
    (void)display_manager_write_line(4U, "POTENCIA %.1f W", (double)ctx->meter.active_power_w);
    (void)display_manager_write_line(5U, "FP %.2f  F %.1fHZ", (double)ctx->meter.power_factor, (double)ctx->meter.frequency);
    (void)display_manager_write_line(6U, "CONSUMO  %.2f WH", (double)ctx->meter.energy_wh);
    (void)display_manager_write_line(7U, "AC:%s M:%s A:%s",
                                     display_manager_ac_badge(ctx),
                                     ctx->mqtt_connected ? "OK" : "--",
                                     display_manager_ade_status(ctx));
    return ESP_OK;
}

static esp_err_t display_manager_render_energy(const ui_display_context_t *ctx)
{
    (void)display_manager_write_line(0U, "ENERGIA");
    (void)display_manager_write_line(1U, "----------------");
    (void)display_manager_write_line(2U, "TOTAL  %.3f WH", (double)ctx->meter.energy_wh);
    (void)display_manager_write_line(3U, "NVS    %.3f WH", (double)ctx->meter.energy_total_nvs_wh);
    (void)display_manager_write_line(4U, "SOFT   %.3f WH", (double)ctx->meter.energy_soft_wh);
    (void)display_manager_write_line(5U, "HW ADE %.3f WH", (double)ctx->meter.energy_hw_wh);
    (void)display_manager_write_line(6U, "REACT  %.3f VARH", (double)ctx->meter.energy_varh);
    return display_manager_write_line(7U, "UP/DN:NAV BACK:MENU");
}

static esp_err_t display_manager_render_comms(const ui_display_context_t *ctx)
{
    (void)display_manager_write_line(0U, "    ESTADO NODOS");
    (void)display_manager_write_line(1U, "----------------");
    (void)display_manager_write_line(2U, "ADE9153A   : %s",
                                     (ctx->ade.ready && !ctx->ade.bus_disconnected) ? "OK" : "ERROR");
    (void)display_manager_write_line(3U, "SIM7080G   : %s",
                                     ctx->cellular_connected ? "CONECTADO" : "SIN RED");
    (void)display_manager_write_line(4U, "MQTT BROKER: %s",
                                     ctx->mqtt_connected ? "CONECTADO" : "DESCONECT");
    (void)display_manager_write_line(5U, "BUS SPI    : %s",
                                     display_manager_bus_status(ctx));
    (void)display_manager_write_line(6U, "LINEA AC   : %s",
                                     display_manager_ac_badge(ctx));
    return display_manager_write_line(7U, "REC:%s CNT:%02lu",
                                     display_manager_action_name(ctx->ade.last_action),
                                     display_manager_count_2d(ctx->ade.recovery_count));
}

static esp_err_t display_manager_render_ade(const ui_display_context_t *ctx)
{
    char temp_text[12] = {0};
    (void)display_manager_write_line(0U, "    DIAGNOSTICO");
    (void)display_manager_write_line(1U, "----------------");
    (void)display_manager_write_line(2U, "CHIP  : %s",
                                     ctx->ade.ready ? "OK" : "FALLO");
    (void)display_manager_write_line(3U, "BUS   : %s",
                                     ctx->ade.bus_disconnected ? "DESCONECTADO" : "CONECTADO");
    (void)display_manager_write_line(4U, "TEMP  : %s",
                                     display_manager_temp_text(&ctx->meter, temp_text, sizeof(temp_text)));
    (void)display_manager_write_line(5U, "VER:%04X RUN:%04X",
                                     (unsigned)ctx->ade.version,
                                     (unsigned)ctx->ade.run);
    (void)display_manager_write_line(6U, "RECOV : %lu  ULT:%s",
                                     (unsigned long)ctx->ade.recovery_count,
                                     display_manager_action_name(ctx->ade.last_action));
    return display_manager_write_line(7U, "ERR: %ld",
                                     (long)ctx->ade.probe_err);
}

static esp_err_t display_manager_render_actions(const ui_menu_state_t *state, const ui_display_context_t *ctx)
{
    (void)display_manager_write_line(0U, "ACCIONES ADE");
    (void)display_manager_write_line(1U, "%c PROBE ADE", (state->action_index == 0U) ? '>' : ' ');
    (void)display_manager_write_line(2U, "%c SOFT INIT", (state->action_index == 1U) ? '>' : ' ');
    (void)display_manager_write_line(3U, "%c SPI REINIT", (state->action_index == 2U) ? '>' : ' ');
    (void)display_manager_write_line(4U, "%c CLEAR FAULT", (state->action_index == 3U) ? '>' : ' ');
    (void)display_manager_write_line(5U, "%c CALIBRAR", (state->action_index == 4U) ? '>' : ' ');
    (void)display_manager_write_line(6U, "%s", ctx->action_status);
    return display_manager_write_line(7U, "SEL:OK HOLD:MENU");
}

esp_err_t display_manager_show_splash(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = oled_driver_clear();
    if (err != ESP_OK) {
        return err;
    }

    (void)display_manager_write_line(0U, "");
    (void)display_manager_write_line(1U, "  MEDIDOR");
    (void)display_manager_write_line(2U, "  INTELIGENTE");
    (void)display_manager_write_line(3U, "");
    (void)display_manager_write_line(4U, "ID: %s", METER_MQTT_CLIENT_ID);
    (void)display_manager_write_line(5U, "");
    (void)display_manager_write_line(6U, "  PUJ  FW %s", METER_FW_VERSION);
    (void)display_manager_write_line(7U, "  INICIANDO...");

    return oled_driver_flush();
}

esp_err_t display_manager_init(void)
{
    const esp_err_t err = oled_driver_init();
    if (err != ESP_OK) {
        return err;
    }
    s_initialized = true;
    return ESP_OK;
}

esp_err_t display_manager_render(const ui_menu_state_t *state,
                                 const ui_display_context_t *context)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((state == NULL) || (context == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = oled_driver_clear();
    if (err != ESP_OK) {
        s_initialized = false;
        return err;
    }

    if (state->view == UI_VIEW_HOME) {
        err = display_manager_render_home(context);
    } else if (state->view == UI_VIEW_ROOT) {
        err = display_manager_render_root(state);
    } else {
        switch (state->screen) {
            case UI_SCREEN_OVERVIEW:
                err = display_manager_render_overview(context);
                break;
            case UI_SCREEN_ENERGY:
                err = display_manager_render_energy(context);
                break;
            case UI_SCREEN_COMMS:
                err = display_manager_render_comms(context);
                break;
            case UI_SCREEN_ADE:
                err = display_manager_render_ade(context);
                break;
            case UI_SCREEN_ACTIONS:
                err = display_manager_render_actions(state, context);
                break;
            default:
                err = display_manager_write_line(0U, "UNKNOWN SCREEN");
                break;
        }
    }

    if (err != ESP_OK) {
        s_initialized = false;
        return err;
    }

    err = oled_driver_flush();
    if (err != ESP_OK) {
        s_initialized = false;
    }
    return err;
}
