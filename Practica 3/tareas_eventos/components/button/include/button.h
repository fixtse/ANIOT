#pragma once

#include "driver/gpio.h"
#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(BUTTON_EVENT);
enum { BUTTON_PRESSED };

esp_err_t button_init(gpio_num_t pin, unsigned int interval_seconds);

// Publica el mismo evento que una pulsación física; llamar desde una tarea tras la inicialización.
esp_err_t button_simulate_press(void);
