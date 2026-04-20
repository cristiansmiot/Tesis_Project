#include "menu_handler.h"

#include <string.h>
#include "esp_timer.h"
#include "meter_config.h"

static ui_menu_state_t s_state;
static int64_t s_last_input_us;

static ui_screen_t menu_handler_root_to_screen(uint8_t index)
{
    if (index >= (uint8_t)UI_SCREEN_COUNT) {
        return UI_SCREEN_OVERVIEW;
    }
    return (ui_screen_t)index;
}

static uint8_t menu_handler_screen_to_root(ui_screen_t screen)
{
    if (screen >= UI_SCREEN_COUNT) {
        return 0U;
    }
    return (uint8_t)screen;
}

esp_err_t menu_handler_init(void)
{
    (void)memset(&s_state, 0, sizeof(s_state));
    s_state.view = UI_VIEW_HOME;
    s_state.screen = UI_SCREEN_OVERVIEW;
    s_state.dirty = true;
    s_last_input_us = esp_timer_get_time();
    return ESP_OK;
}

void menu_handler_process_event(keypad_event_t event)
{
    if (event == KEYPAD_EVENT_NONE) {
        return;
    }

    s_last_input_us = esp_timer_get_time();

    if (s_state.view == UI_VIEW_HOME) {
        s_state.view = UI_VIEW_ROOT;
        s_state.dirty = true;
        return;
    }

    if (s_state.view == UI_VIEW_ROOT) {
        if (event == KEYPAD_EVENT_UP) {
            s_state.root_index = (s_state.root_index == 0U) ?
                ((uint8_t)UI_SCREEN_COUNT - 1U) :
                (uint8_t)(s_state.root_index - 1U);
            s_state.dirty = true;
        } else if (event == KEYPAD_EVENT_DOWN) {
            s_state.root_index = (uint8_t)((s_state.root_index + 1U) % (uint8_t)UI_SCREEN_COUNT);
            s_state.dirty = true;
        } else if ((event == KEYPAD_EVENT_SELECT) || (event == KEYPAD_EVENT_BACK)) {
            s_state.screen = menu_handler_root_to_screen(s_state.root_index);
            s_state.view = UI_VIEW_SCREEN;
            s_state.dirty = true;
        }
        return;
    }

    if (event == KEYPAD_EVENT_BACK) {
        s_state.view = UI_VIEW_ROOT;
        s_state.root_index = menu_handler_screen_to_root(s_state.screen);
        s_state.dirty = true;
        return;
    }

    if (s_state.screen == UI_SCREEN_ACTIONS) {
        if (event == KEYPAD_EVENT_UP) {
            s_state.action_index = (s_state.action_index == 0U) ? 4U : (uint8_t)(s_state.action_index - 1U);
            s_state.dirty = true;
        } else if (event == KEYPAD_EVENT_DOWN) {
            s_state.action_index = (uint8_t)((s_state.action_index + 1U) % 5U);
            s_state.dirty = true;
        } else if (event == KEYPAD_EVENT_SELECT) {
            switch (s_state.action_index) {
                case 0:
                    s_state.pending_action = UI_ACTION_ADE_PROBE;
                    break;
                case 1:
                    s_state.pending_action = UI_ACTION_ADE_SOFT_RECOVER;
                    break;
                case 2:
                    s_state.pending_action = UI_ACTION_ADE_SPI_REINIT;
                    break;
                case 3:
                    s_state.pending_action = UI_ACTION_CLEAR_LAST_FAULT;
                    break;
                case 4:
                    s_state.pending_action = UI_ACTION_ADE_CALIBRATE;
                    break;
                default:
                    s_state.pending_action = UI_ACTION_NONE;
                    break;
            }
            s_state.dirty = true;
        }
        return;
    }

    if (event == KEYPAD_EVENT_UP) {
        s_state.screen = (s_state.screen == UI_SCREEN_OVERVIEW) ?
            UI_SCREEN_ACTIONS :
            (ui_screen_t)(s_state.screen - 1);
        s_state.root_index = menu_handler_screen_to_root(s_state.screen);
        s_state.dirty = true;
    } else if (event == KEYPAD_EVENT_DOWN) {
        s_state.screen = (ui_screen_t)((s_state.screen + 1U) % UI_SCREEN_COUNT);
        s_state.root_index = menu_handler_screen_to_root(s_state.screen);
        s_state.dirty = true;
    } else if (event == KEYPAD_EVENT_SELECT) {
        s_state.view = UI_VIEW_ROOT;
        s_state.root_index = menu_handler_screen_to_root(s_state.screen);
        s_state.dirty = true;
    }
}

esp_err_t menu_handler_get_state(ui_menu_state_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = s_state;
    return ESP_OK;
}

ui_action_t menu_handler_take_pending_action(void)
{
    const ui_action_t action = s_state.pending_action;
    s_state.pending_action = UI_ACTION_NONE;
    return action;
}

bool menu_handler_consume_dirty(void)
{
    const bool dirty = s_state.dirty;
    s_state.dirty = false;
    return dirty;
}

const char *menu_handler_get_screen_name(ui_screen_t screen)
{
    switch (screen) {
        case UI_SCREEN_OVERVIEW:
            return "MEDICIONES";
        case UI_SCREEN_ENERGY:
            return "ENERGIA";
        case UI_SCREEN_COMMS:
            return "ESTADO NODOS";
        case UI_SCREEN_ADE:
            return "DIAGNOSTICO";
        case UI_SCREEN_ACTIONS:
            return "ACCIONES";
        default:
            return "UNKNOWN";
    }
}

bool menu_handler_check_idle_home(void)
{
    if (s_state.view == UI_VIEW_HOME) {
        return false;
    }
    const int64_t now_us = esp_timer_get_time();
    const int64_t idle_us = (int64_t)METER_UI_IDLE_HOME_TIMEOUT_MS * 1000LL;
    if ((now_us - s_last_input_us) >= idle_us) {
        s_state.view = UI_VIEW_HOME;
        s_state.dirty = true;
        return true;
    }
    return false;
}
