/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "shtc3.h"

static const char *TAG = "SHTC3_APP";

#define SAMPLE_PERIOD_MS 2000U

typedef struct {
    float temperature;
    float humidity;
    bool available;
    uint32_t generation;
} sensor_sample_t;

static shtc3_t temp_sensor;
static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static sensor_sample_t latest_sample = {0};
static portMUX_TYPE sample_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t sampling_period_ms = SAMPLE_PERIOD_MS;

static void delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void init_i2c(void)
{
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = 8,
        .sda_io_num = 10,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle));
}

static void sampling_task(void *arg)
{
    const uint32_t period_ms = *(const uint32_t *)arg;

    while (1) {
        float temperature;
        float humidity;

        if (shtc3_get_temp_and_hum_lpm(&temp_sensor, &temperature, &humidity) != 0) {
            ESP_LOGE(TAG, "Error leyendo temperatura y humedad");
        } else {
            portENTER_CRITICAL(&sample_mux);
            latest_sample.temperature = temperature;
            latest_sample.humidity = humidity;
            latest_sample.available = true;
            latest_sample.generation++;
            portEXIT_CRITICAL(&sample_mux);
        }

        vTaskDelay(pdMS_TO_TICKS(period_ms));
    }
}

void app_main(void)
{
    uint32_t displayed_generation = 0;

    init_i2c();

    if (shtc3_init(&temp_sensor, i2c_bus_handle, SHTC3_I2C_ADDR, delay_ms) != 0) {
        ESP_LOGE(TAG, "Error al inicializar el sensor SHTC3");
        return;
    }

    if (xTaskCreate(sampling_task, "shtc3_sampling", 3072,
                    (void *)&sampling_period_ms, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear la tarea de muestreo");
        return;
    }

    while (1) {
        sensor_sample_t sample;

        portENTER_CRITICAL(&sample_mux);
        sample = latest_sample;
        portEXIT_CRITICAL(&sample_mux);

        if (sample.available && sample.generation != displayed_generation) {
            ESP_LOGI(TAG, "Temperatura: %.2f °C, Humedad: %.2f %%",
                     sample.temperature, sample.humidity);
            displayed_generation = sample.generation;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
