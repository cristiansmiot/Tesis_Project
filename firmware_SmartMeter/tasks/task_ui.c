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
#include "rtos_app_config.h"
#include "storage_service.h"
#include "task_calibration.h"
#include "task_manager.h"
#include "task_monitor.h"
#include "system_health.h"
#include "time_service.h"

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
#if (METER_SIM7080G_FOCUS_MODE == 0)
    (void)ade9153a_get_diag(&ctx->ade);
#endif
    (void)system_health_get_all_snapshots(ctx->health, SYSTEM_MODULE_COUNT);
    (void)storage_service_get_snapshot(&ctx->storage);
    ctx->cellular_connected =
        (ctx->health[SYSTEM_MODULE_CELLULAR].state == SYSTEM_STATE_OK);
    ctx->mqtt_connected =
        (ctx->health[SYSTEM_MODULE_MQTT].state == SYSTEM_STATE_OK);
    ctx->primary_alert_code = SYSTEM_EVENT_CODE_NONE;
    ctx->primary_alert_severity = SYSTEM_SEVERITY_INFO;
    for (int i = 0; i < (int)SYSTEM_MODULE_COUNT; ++i) {
        const system_health_snapshot_t *health = &ctx->health[i];
        if ((health->active_event != SYSTEM_EVENT_CODE_NONE) &&
            ((ctx->primary_alert_code == SYSTEM_EVENT_CODE_NONE) ||
             (health->active_severity > ctx->primary_alert_severity))) {
            ctx->primary_alert_module = (system_module_t)i;
            ctx->primary_alert_code = health->active_event;
            ctx->primary_alert_severity = health->active_severity;
        }
    }
    (void)snprintf(ctx->action_status, sizeof(ctx->action_status), "%s",
                   (s_action_status[0] != '\0') ? s_action_status : "READY");

    system_time_status_t time_status = {0};
    ctx->time_valid = system_time_is_valid();
    if (ctx->time_valid &&
        (time_service_format_bogota(ctx->date_str, sizeof(ctx->date_str),
                                    false) == ESP_OK)) {
        if (system_time_get_status(&time_status) == ESP_OK) {
            (void)snprintf(ctx->time_source, sizeof(ctx->time_source), "%s",
                           time_service_source_name(time_status.source));
        }
    } else {
        ctx->date_str[0] = '\0';
        (void)snprintf(ctx->time_source, sizeof(ctx->time_source), "NONE");
    }
}

static void task_ui_execute_action(ui_action_t action)
{
#if (METER_SIM7080G_FOCUS_MODE != 0)
    if ((action == UI_ACTION_ADE_PROBE) ||
        (action == UI_ACTION_ADE_SOFT_RECOVER) ||
        (action == UI_ACTION_ADE_SPI_REINIT) ||
        (action == UI_ACTION_ADE_CALIBRATE)) {
        task_ui_set_status("ADE DISABLED");
        return;
    }
#endif
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
                (void)system_health_report_ok(SYSTEM_MODULE_ADE,
                                              "ADE manual soft recovery");
                task_ui_set_status("INIT OK");
            } else {
                task_ui_set_status("INIT FAIL");
            }
            break;
        case UI_ACTION_ADE_SPI_REINIT:
            if (ade9153a_recover(ADE9153A_RECOVERY_ACTION_SPI_REINIT) == ESP_OK) {
                (void)system_health_report_ok(SYSTEM_MODULE_ADE,
                                              "ADE manual SPI recovery");
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
        (void)system_health_set_disabled(SYSTEM_MODULE_UI, "UI disabled");
        (void)system_health_set_disabled(SYSTEM_MODULE_OLED, "UI disabled");
        (void)system_health_set_disabled(SYSTEM_MODULE_INPUT, "UI disabled");
        vTaskDelete(NULL);
        return;
    }

    (void)system_health_mark_recovering(SYSTEM_MODULE_UI, "UI startup");
    bool menu_ready = (menu_handler_init() == ESP_OK);
    if (menu_ready) {
        (void)system_health_report_ok(SYSTEM_MODULE_UI, "UI menu ready");
    } else {
        (void)system_health_report_failure(
            SYSTEM_MODULE_UI, SYSTEM_EVENT_INIT_FAILED,
            SYSTEM_SEVERITY_WARNING, ESP_FAIL, "UI menu init failed");
    }
    bool keypad_ready = false;
    bool display_ready = false;
    bool meter_data_dirty = true;
    bool splash_done = false;
    bool main_screen_logged = false;
    int64_t splash_start_ms = 0;
    int64_t last_keypad_retry_ms = 0;
    int64_t last_display_retry_ms = 0;
    int64_t last_refresh_ms = 0;
    uint8_t display_recovery_attempts = 0U;
    uint32_t display_retry_delay_ms = 0U;
    ui_menu_state_t last_rendered_state = {0};
    ui_display_context_t last_rendered_ctx = {0};
    bool have_last_frame = false;
    task_ui_set_status("UI BOOT");

    for (;;) {
        task_monitor_heartbeat(TASK_MON_UI);
        const int64_t now_ms = task_ui_now_ms();

        if (!menu_ready) {
            menu_ready = (menu_handler_init() == ESP_OK);
            if (menu_ready) {
                (void)system_health_report_ok(SYSTEM_MODULE_UI,
                                              "UI menu recovered");
            }
        }
        if (!keypad_ready && ((now_ms - last_keypad_retry_ms) >= 2000LL)) {
            (void)system_health_mark_recovering(SYSTEM_MODULE_INPUT,
                                                "keypad init");
            const esp_err_t keypad_err = keypad_driver_init();
            keypad_ready = (keypad_err == ESP_OK);
            if (keypad_ready) {
                (void)system_health_report_ok(SYSTEM_MODULE_INPUT,
                                              "keypad ready");
                task_ui_set_status("KEYPAD OK");
            } else {
                (void)system_health_report_failure(
                    SYSTEM_MODULE_INPUT, SYSTEM_EVENT_INIT_FAILED,
                    SYSTEM_SEVERITY_WARNING, keypad_err,
                    "keypad init failed");
            }
            last_keypad_retry_ms = now_ms;
        }
        if (!display_ready &&
            ((now_ms - last_display_retry_ms) >= (int64_t)display_retry_delay_ms)) {
            (void)system_health_mark_recovering(SYSTEM_MODULE_OLED,
                                                "OLED init");
            const esp_err_t display_err = display_manager_init();
            display_ready = (display_err == ESP_OK);
            if (display_ready) {
                display_recovery_attempts = 0U;
                display_retry_delay_ms = 0U;
                have_last_frame = false;
                (void)system_health_report_ok(SYSTEM_MODULE_OLED,
                                              "OLED ready");
                (void)display_manager_show_splash();
                splash_start_ms = now_ms;
                task_ui_set_status("DISPLAY OK");
            } else {
                (void)system_health_report_failure(
                    SYSTEM_MODULE_OLED, SYSTEM_EVENT_INIT_FAILED,
                    SYSTEM_SEVERITY_WARNING, display_err,
                    "OLED init failed");
                if (display_recovery_attempts < UINT8_MAX) {
                    ++display_recovery_attempts;
                }
                if (display_recovery_attempts == 1U) {
                    display_retry_delay_ms = SYSTEM_RECOVERY_DELAY_1_MS;
                } else if (display_recovery_attempts == 2U) {
                    display_retry_delay_ms = SYSTEM_RECOVERY_DELAY_2_MS;
                } else if (display_recovery_attempts == 3U) {
                    display_retry_delay_ms = SYSTEM_RECOVERY_DELAY_3_MS;
                } else {
                    display_retry_delay_ms = SYSTEM_RECOVERY_SLOW_PROBE_MS;
                }
            }
            last_display_retry_ms = now_ms;
        }

        bool splash_just_finished = false;
        if (display_ready && !splash_done) {
            if ((now_ms - splash_start_ms) < (int64_t)METER_UI_SPLASH_DURATION_MS) {
                vTaskDelay(pdMS_TO_TICKS(METER_UI_INPUT_POLL_PERIOD_MS));
                continue;
            }
            splash_done = true;
            splash_just_finished = true;
            LOG_INFO(TAG, "splash finalizado; renderizando pantalla principal");
        }

        /* No depender exclusivamente del flag dirty para abandonar el splash:
           el primer frame de la aplicacion debe renderizarse siempre. */
        bool should_render = splash_just_finished;
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
        /* El estado celular/MQTT cambia sin generar MeterEvent_t. Refrescar
           periodicamente incluso cuando la metrologia esta deshabilitada
           (perfil test_all_no_ade), para no dejar indicadores obsoletos. */
        const bool periodic_refresh_due =
            ((now_ms - last_refresh_ms) >= (int64_t)METER_UI_PERIODIC_REFRESH_MS);
        if (meter_data_dirty || periodic_refresh_due) {
            should_render = true;
            if (periodic_refresh_due) {
                last_refresh_ms = now_ms;
            }
            meter_data_dirty = false;
        }

        if (display_ready && should_render) {
            ui_menu_state_t state = {0};
            ui_display_context_t ctx = {0};
            if (menu_handler_get_state(&state) == ESP_OK) {
                task_ui_build_context(&ctx);
                const bool frame_changed = !have_last_frame ||
                    (memcmp(&state, &last_rendered_state, sizeof(state)) != 0) ||
                    (memcmp(&ctx, &last_rendered_ctx, sizeof(ctx)) != 0);
                if (!frame_changed) {
                    continue;
                }
                if (display_manager_render(&state, &ctx) != ESP_OK) {
                    display_ready = false;
                    display_retry_delay_ms = SYSTEM_RECOVERY_DELAY_1_MS;
                    display_recovery_attempts = 1U;
                    (void)system_health_report_failure(
                        SYSTEM_MODULE_OLED, SYSTEM_EVENT_IO_ERROR,
                        SYSTEM_SEVERITY_WARNING, ESP_FAIL,
                        "OLED render failed");
                    task_ui_set_status("DISPLAY LOST");
                    LOG_WARN(TAG, "display render failed - will retry init");
                } else {
                    last_rendered_state = state;
                    last_rendered_ctx = ctx;
                    have_last_frame = true;
                    if (!main_screen_logged) {
                        main_screen_logged = true;
                        LOG_INFO(TAG, "pantalla principal activa");
                    }
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
