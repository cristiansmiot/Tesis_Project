#include "task_ui.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "ade9153a_init.h"
#include "display_manager.h"
#include "esp_timer.h"
#include "fault_handler.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "keypad_driver.h"
#include "logger.h"
#include "menu_handler.h"
#include "meter_config.h"
#include "meter_data.h"
#include "node_health.h"
#include "sim7080g_init.h"
#include "../modules/communication/mqtt_client.h"
#include "rtos_app_config.h"
#include "task_calibration.h"
#include "task_manager.h"

static const char *TAG = "task_ui";
static TaskHandle_t s_task_handle;
static char s_action_status[32] = "BOOT";
static int64_t s_action_status_set_ms;

static int64_t task_ui_now_ms(void)
{
    return esp_timer_get_time() / 1000LL;
}

static void task_ui_set_status(const char *text)
{
    if (text == NULL) {
        return;
    }
    (void)snprintf(s_action_status, sizeof(s_action_status), "%s", text);
    s_action_status_set_ms = task_ui_now_ms();
}

static void task_ui_clear_status_if_expired(void)
{
    if ((s_action_status[0] == '\0') || (s_action_status_set_ms == 0)) {
        return;
    }
    if ((task_ui_now_ms() - s_action_status_set_ms) >= (int64_t)METER_UI_ACTION_STATUS_HOLD_MS) {
        s_action_status[0] = '\0';
        s_action_status_set_ms = 0;
    }
}

static void task_ui_build_context(ui_display_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    (void)memset(ctx, 0, sizeof(*ctx));
    ctx->meter = meter_data_get_snapshot();
    (void)ade9153a_get_diag(&ctx->ade);
    ctx->cellular_connected = sim7080g_is_network_ready() && sim7080g_is_pdp_ready();
    ctx->mqtt_connected = mqtt_client_is_connected();
    ctx->cellular_attempts = node_health_get_cellular_attempts();
    ctx->cellular_successes = node_health_get_cellular_successes();
    ctx->mqtt_attempts = node_health_get_mqtt_attempts();
    ctx->mqtt_successes = node_health_get_mqtt_successes();
    ctx->ade_losses = node_health_get_ade_losses();
    ctx->ade_recovery_successes = node_health_get_ade_recovery_successes();
    (void)snprintf(ctx->action_status, sizeof(ctx->action_status), "%s",
                   (s_action_status[0] != '\0') ? s_action_status : "READY");

    const char *net_time = sim7080g_get_network_time();
    if ((net_time != NULL) && (net_time[0] != '\0')) {
        (void)snprintf(ctx->date_str, sizeof(ctx->date_str), "%s", net_time);
    } else {
        ctx->date_str[0] = '\0';
    }
}

static void task_ui_execute_action(ui_action_t action)
{
    switch (action) {
        case UI_ACTION_ADE_PROBE:
            if (ade9153a_recover(ADE9153A_RECOVERY_ACTION_PROBE) == ESP_OK) {
                task_ui_set_status("PROBE OK");
            } else {
                task_ui_set_status("PROBE FAIL");
            }
            break;
        case UI_ACTION_ADE_SOFT_RECOVER:
            if (ade9153a_recover(ADE9153A_RECOVERY_ACTION_SOFT_INIT) == ESP_OK) {
                fault_clear_last();
                task_ui_set_status("INIT OK");
            } else {
                task_ui_set_status("INIT FAIL");
            }
            break;
        case UI_ACTION_ADE_SPI_REINIT:
            if (ade9153a_recover(ADE9153A_RECOVERY_ACTION_SPI_REINIT) == ESP_OK) {
                fault_clear_last();
                task_ui_set_status("SPI OK");
            } else {
                task_ui_set_status("SPI FAIL");
            }
            break;
        case UI_ACTION_CLEAR_LAST_FAULT:
            fault_clear_last();
            task_ui_set_status("LAST CLEARED");
            break;
        case UI_ACTION_ADE_CALIBRATE:
            {
                uint8_t cal_st = task_calibration_get_status();
                if ((cal_st & CALIB_STATUS_RUNNING) != 0U) {
                    task_ui_set_status("CAL RUNNING...");
                } else if (task_calibration_start() == ESP_OK) {
                    task_ui_set_status("CAL STARTED");
                } else {
                    task_ui_set_status("CAL FAIL");
                }
            }
            break;
        case UI_ACTION_NONE:
        default:
            break;
    }
}

static void task_ui(void *pvParameters)
{
    (void)pvParameters;

    if (!METER_UI_ENABLE) {
        LOG_WARN(TAG, "UI disabled by config");
        vTaskDelete(NULL);
        return;
    }

    bool menu_ready = (menu_handler_init() == ESP_OK);
    bool keypad_ready = false;
    bool display_ready = false;
    bool meter_data_dirty = true;
    bool splash_done = false;
    int64_t splash_start_ms = 0;
    int64_t last_keypad_retry_ms = 0;
    int64_t last_display_retry_ms = 0;
    int64_t last_refresh_ms = 0;
    task_ui_set_status("UI BOOT");

    for (;;) {
        const int64_t now_ms = task_ui_now_ms();

        if (!menu_ready) {
            menu_ready = (menu_handler_init() == ESP_OK);
        }
        if (!keypad_ready && ((now_ms - last_keypad_retry_ms) >= 2000LL)) {
            keypad_ready = (keypad_driver_init() == ESP_OK);
            if (keypad_ready) {
                task_ui_set_status("KEYPAD OK");
            }
            last_keypad_retry_ms = now_ms;
        }
        if (!display_ready && ((now_ms - last_display_retry_ms) >= 2000LL)) {
            display_ready = (display_manager_init() == ESP_OK);
            if (display_ready) {
                (void)display_manager_show_splash();
                splash_start_ms = now_ms;
                task_ui_set_status("DISPLAY OK");
            }
            last_display_retry_ms = now_ms;
        }

        if (display_ready && !splash_done) {
            if ((now_ms - splash_start_ms) < (int64_t)METER_UI_SPLASH_DURATION_MS) {
                vTaskDelay(pdMS_TO_TICKS(METER_UI_INPUT_POLL_PERIOD_MS));
                continue;
            }
            splash_done = true;
        }

        bool should_render = false;
        MeterEvent_t event = {0};
        if (q_meter_events != NULL) {
            if (xQueueReceive(q_meter_events, &event, pdMS_TO_TICKS(METER_UI_INPUT_POLL_PERIOD_MS)) == pdTRUE) {
                meter_data_dirty = true;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(METER_UI_INPUT_POLL_PERIOD_MS));
        }

        if (keypad_ready) {
            const keypad_event_t key_event = keypad_driver_poll_event();
            if (key_event != KEYPAD_EVENT_NONE) {
                menu_handler_process_event(key_event);
                should_render = true;
            }
        }

        if (menu_handler_consume_dirty()) {
            should_render = true;
        }

        const ui_action_t action = menu_handler_take_pending_action();
        if (action != UI_ACTION_NONE) {
            task_ui_execute_action(action);
            should_render = true;
        }

        task_ui_clear_status_if_expired();
        if (menu_handler_check_idle_home()) {
            should_render = true;
        }
        if (meter_data_dirty && ((now_ms - last_refresh_ms) >= (int64_t)METER_UI_PERIODIC_REFRESH_MS)) {
            should_render = true;
            last_refresh_ms = now_ms;
            meter_data_dirty = false;
        }

        if (display_ready && should_render) {
            ui_menu_state_t state = {0};
            ui_display_context_t ctx = {0};
            if (menu_handler_get_state(&state) == ESP_OK) {
                task_ui_build_context(&ctx);
                if (display_manager_render(&state, &ctx) != ESP_OK) {
                    display_ready = false;
                    task_ui_set_status("DISPLAY LOST");
                    LOG_WARN(TAG, "display render failed - will retry init");
                }
            }
        }
    }
}

esp_err_t task_ui_start(void)
{
    if (s_task_handle != NULL) {
        return ESP_OK;
    }

    const BaseType_t rc = xTaskCreatePinnedToCore(
        task_ui,
        "task_ui",
        TASK_STACK_UI,
        NULL,
        TASK_PRIO_UI,
        &s_task_handle,
        1);

    if (rc != pdPASS) {
        LOG_ERROR(TAG, "task_ui create failed");
        s_task_handle = NULL;
        return ESP_FAIL;
    }

    LOG_INFO(TAG, "task_ui started on core 1");
    return ESP_OK;
}
