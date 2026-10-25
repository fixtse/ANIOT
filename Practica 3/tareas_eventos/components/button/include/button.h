#pragma once

#include "driver/gpio.h"
#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(BUTTON_EVENT);
enum { BUTTON_PRESSED };

esp_err_t button_init(gpio_num_t pin, unsigned int interval_seconds);

// Post the same event as a physical press. Call from task context after init.
esp_err_t button_simulate_press(void);
