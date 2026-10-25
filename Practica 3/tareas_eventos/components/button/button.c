#include "button.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

ESP_EVENT_DEFINE_BASE(BUTTON_EVENT);

static gpio_num_t button_pin;
static TickType_t poll_interval;

esp_err_t button_simulate_press(void)
{
    return esp_event_post(BUTTON_EVENT, BUTTON_PRESSED, NULL, 0, portMAX_DELAY);
}

static void button_task(void *arg)
{
    bool was_pressed = false;
    while (1) {
        bool pressed = gpio_get_level(button_pin) == 0;
        if (pressed && !was_pressed) {
            vTaskDelay(pdMS_TO_TICKS(50));
            pressed = gpio_get_level(button_pin) == 0;
            if (pressed) {
                ESP_ERROR_CHECK(esp_event_post(BUTTON_EVENT, BUTTON_PRESSED,
                                               NULL, 0, portMAX_DELAY));
            }
        }
        was_pressed = pressed;
        vTaskDelay(poll_interval);
    }
}

esp_err_t button_init(gpio_num_t pin, unsigned int interval_ms)
{
    if (interval_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }
    button_pin = pin;
    poll_interval = pdMS_TO_TICKS(interval_ms);
    return xTaskCreate(button_task, "button", 2048, NULL, 5, NULL) == pdPASS
               ? ESP_OK : ESP_ERR_NO_MEM;
}
