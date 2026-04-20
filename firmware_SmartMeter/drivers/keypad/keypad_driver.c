#include "keypad_driver.h"

#include <stdbool.h>
#include <stdint.h>
#include "board_config.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "logger.h"

typedef struct {
    int pin;
    keypad_event_t short_event;
    bool emit_on_press;
    bool stable_pressed;
    bool sample_pressed;
    bool long_emitted;
    int64_t last_change_ms;
    int64_t pressed_since_ms;
} keypad_button_t;

static const char *TAG = "keypad_driver";
static bool s_initialized;
static keypad_button_t s_buttons[3];

static int64_t keypad_driver_now_ms(void)
{
    return esp_timer_get_time() / 1000LL;
}

static keypad_event_t keypad_driver_process_button(keypad_button_t *button, bool pressed, int64_t now_ms)
{
    if (button == NULL) {
        return KEYPAD_EVENT_NONE;
    }

    if (pressed != button->sample_pressed) {
        button->sample_pressed = pressed;
        button->last_change_ms = now_ms;
    }

    if ((now_ms - button->last_change_ms) < (int64_t)BUTTON_DEBOUNCE_MS) {
        return KEYPAD_EVENT_NONE;
    }

    if (pressed != button->stable_pressed) {
        button->stable_pressed = pressed;
        if (pressed) {
            button->pressed_since_ms = now_ms;
            button->long_emitted = false;
            if (button->emit_on_press) {
                return button->short_event;
            }
        } else {
            if (button->emit_on_press) {
                return KEYPAD_EVENT_NONE;
            }
            if ((button->short_event == KEYPAD_EVENT_SELECT) && button->long_emitted) {
                return KEYPAD_EVENT_NONE;
            }
            return button->short_event;
        }
    }

    if ((button->short_event == KEYPAD_EVENT_SELECT) &&
        button->stable_pressed &&
        !button->long_emitted &&
        ((now_ms - button->pressed_since_ms) >= (int64_t)BUTTON_LONGPRESS_MS)) {
        button->long_emitted = true;
        return KEYPAD_EVENT_BACK;
    }

    return KEYPAD_EVENT_NONE;
}

esp_err_t keypad_driver_init(void)
{
    (void)gpio_reset_pin((gpio_num_t)PIN_BTN_UP);
    (void)gpio_reset_pin((gpio_num_t)PIN_BTN_SELECT);
    (void)gpio_reset_pin((gpio_num_t)PIN_BTN_DOWN);

    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_BTN_UP) | (1ULL << PIN_BTN_SELECT) | (1ULL << PIN_BTN_DOWN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        LOG_WARN(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }

    const int64_t now_ms = keypad_driver_now_ms();
    const bool up_pressed = (gpio_get_level((gpio_num_t)PIN_BTN_UP) == 0);
    const bool select_pressed = (gpio_get_level((gpio_num_t)PIN_BTN_SELECT) == 0);
    const bool down_pressed = (gpio_get_level((gpio_num_t)PIN_BTN_DOWN) == 0);

    s_buttons[0] = (keypad_button_t){
        .pin = PIN_BTN_UP,
        .short_event = KEYPAD_EVENT_UP,
        .emit_on_press = true,
        .stable_pressed = up_pressed,
        .sample_pressed = up_pressed,
        .last_change_ms = now_ms,
    };
    s_buttons[1] = (keypad_button_t){
        .pin = PIN_BTN_SELECT,
        .short_event = KEYPAD_EVENT_SELECT,
        .emit_on_press = false,
        .stable_pressed = select_pressed,
        .sample_pressed = select_pressed,
        .last_change_ms = now_ms,
    };
    s_buttons[2] = (keypad_button_t){
        .pin = PIN_BTN_DOWN,
        .short_event = KEYPAD_EVENT_DOWN,
        .emit_on_press = true,
        .stable_pressed = down_pressed,
        .sample_pressed = down_pressed,
        .last_change_ms = now_ms,
    };
    s_initialized = true;
    LOG_INFO(TAG,
             "keypad ready up=%d(%d) select=%d(%d) down=%d(%d)",
             PIN_BTN_UP, up_pressed ? 0 : 1,
             PIN_BTN_SELECT, select_pressed ? 0 : 1,
             PIN_BTN_DOWN, down_pressed ? 0 : 1);
    return ESP_OK;
}

keypad_event_t keypad_driver_poll_event(void)
{
    if (!s_initialized) {
        return KEYPAD_EVENT_NONE;
    }

    const int64_t now_ms = keypad_driver_now_ms();
    for (size_t i = 0U; i < 3U; ++i) {
        const bool pressed = (gpio_get_level((gpio_num_t)s_buttons[i].pin) == 0);
        const keypad_event_t event = keypad_driver_process_button(&s_buttons[i], pressed, now_ms);
        if (event != KEYPAD_EVENT_NONE) {
            LOG_INFO(TAG, "key event=%d pin=%d", (int)event, s_buttons[i].pin);
            return event;
        }
    }

    return KEYPAD_EVENT_NONE;
}
