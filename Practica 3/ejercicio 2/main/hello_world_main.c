/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "shtc3.h"

static const char *TAG = "SHTC3_QUEUE_APP";

#define SAMPLE_PERIOD_MS 2000U
#define SAMPLE_QUEUE_LENGTH 5U

typedef struct {
    float temperature;
    float humidity;
} sensor_sample_t;

typedef struct {
    uint32_t period_ms;
    QueueHandle_t sample_queue;
} sampling_task_args_t;

static shtc3_t temp_sensor;
static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static sampling_task_args_t sampling_args;

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
    const sampling_task_args_t *args = (const sampling_task_args_t *)arg;

    while (1) {
        sensor_sample_t sample;

        if (shtc3_get_temp_and_hum_lpm(&temp_sensor,
                                       &sample.temperature,
                                       &sample.humidity) != 0) {
            ESP_LOGE(TAG, "Error leyendo temperatura y humedad");
        } else if (xQueueSend(args->sample_queue, &sample, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG, "No se pudo enviar la muestra a la cola");
        }

        vTaskDelay(pdMS_TO_TICKS(args->period_ms));
    }
}

void app_main(void)
{
    init_i2c();

    if (shtc3_init(&temp_sensor, i2c_bus_handle, SHTC3_I2C_ADDR, delay_ms) != 0) {
        ESP_LOGE(TAG, "Error al inicializar el sensor SHTC3");
        return;
    }

    sampling_args.period_ms = SAMPLE_PERIOD_MS;
    sampling_args.sample_queue = xQueueCreate(SAMPLE_QUEUE_LENGTH, sizeof(sensor_sample_t));
    if (sampling_args.sample_queue == NULL) {
        ESP_LOGE(TAG, "No se pudo crear la cola de muestras");
        return;
    }

    if (xTaskCreate(sampling_task, "shtc3_sampling", 3072,
                    &sampling_args, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear la tarea de muestreo");
        vQueueDelete(sampling_args.sample_queue);
        return;
    }

    while (1) {
        sensor_sample_t sample;

        if (xQueueReceive(sampling_args.sample_queue, &sample, portMAX_DELAY) == pdPASS) {
            ESP_LOGI(TAG, "Temperatura: %.2f °C, Humedad: %.2f %%",
                     sample.temperature, sample.humidity);
        }
    }
}
