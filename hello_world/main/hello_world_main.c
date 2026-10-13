/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "shtc3.h"

static const char *TAG = "SHTC3_APP";

static shtc3_t temp_sensor;
static i2c_master_bus_handle_t i2c_bus_handle = NULL;

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

void app_main(void)
{
    float temperature = 0.0f;
    float humidity = 0.0f;

    init_i2c();

    if (shtc3_init(&temp_sensor, i2c_bus_handle, SHTC3_I2C_ADDR, delay_ms) != 0) {
        ESP_LOGE(TAG, "Error al inicializar el sensor SHTC3");
        return;
    }

    while (1) {
        if (shtc3_get_temp_and_hum_lpm(&temp_sensor, &temperature, &humidity) != 0) {
            ESP_LOGE(TAG, "Error leyendo temperatura y humedad");
        } else {
            ESP_LOGI(TAG, "Temperatura: %.2f °C, Humedad: %.2f %%", temperature, humidity);
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
