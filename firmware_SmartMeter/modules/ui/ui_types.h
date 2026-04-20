#ifndef UI_TYPES_H
#define UI_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include "ade9153a_init.h"
#include "meter_data.h"

typedef enum {
    UI_VIEW_ROOT = 0,
    UI_VIEW_SCREEN = 1,
    UI_VIEW_HOME = 2,
} ui_view_t;

typedef enum {
    UI_SCREEN_OVERVIEW = 0,
    UI_SCREEN_ENERGY,
    UI_SCREEN_COMMS,
    UI_SCREEN_ADE,
    UI_SCREEN_ACTIONS,
    UI_SCREEN_COUNT,
} ui_screen_t;

typedef enum {
    UI_ACTION_NONE = 0,
    UI_ACTION_ADE_PROBE,
    UI_ACTION_ADE_SOFT_RECOVER,
    UI_ACTION_ADE_SPI_REINIT,
    UI_ACTION_CLEAR_LAST_FAULT,
    UI_ACTION_ADE_CALIBRATE,
} ui_action_t;

typedef struct {
    ui_view_t view;
    ui_screen_t screen;
    uint8_t root_index;
    uint8_t action_index;
    bool dirty;
    ui_action_t pending_action;
} ui_menu_state_t;

typedef struct {
    MeterData_t meter;
    ade9153a_diag_t ade;
    bool cellular_connected;
    bool mqtt_connected;
    uint32_t cellular_attempts;
    uint32_t cellular_successes;
    uint32_t mqtt_attempts;
    uint32_t mqtt_successes;
    uint32_t ade_losses;
    uint32_t ade_recovery_successes;
    char action_status[32];
    char date_str[20];
} ui_display_context_t;

#endif // UI_TYPES_H
