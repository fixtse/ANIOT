#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_console.h"
#include "linenoise/linenoise.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "sdkconfig.h"

#include "shtc3.h"
#include "simulated_wifi.h"
#include "simulated_flash.h"

static const char *TAG = "MAIN_APP";

typedef enum {
    MODE_MONITORING,
    MODE_CONSOLE
} system_mode_t;

static volatile system_mode_t current_mode = MODE_MONITORING;
static shtc3_t tempSensor;
static i2c_master_bus_handle_t bus_handle;
static TaskHandle_t system_task_handle = NULL; // Task handle for suspension

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_SIM_EVENT) {
        switch (event_id) {
            case WIFI_SIM_EVENT_CONNECTED:
                ESP_LOGI(TAG, "Event: Wi-Fi Connected");
                break;
            case WIFI_SIM_EVENT_IP_ACQUIRED:
                ESP_LOGI(TAG, "Event: IP Acquired! Flushing flash buffer...");
                while (getDataLeft() > 0) {
                    float *val = (float*)readFromFlash(sizeof(float));
                    if (val) {
                        char msg[32];
                        snprintf(msg, sizeof(msg), "%.2f", *val);
                        send_data_wifi(msg, strlen(msg));
                    }
                }
                break;
            case WIFI_SIM_EVENT_DISCONNECTED:
                ESP_LOGI(TAG, "Event: Wi-Fi Disconnected");
                break;
        }
    }
}

static int cmd_monitor(int argc, char **argv) {
    printf("Returning to Monitoring Mode...\n");
    current_mode = MODE_MONITORING;
    wifi_connect();
    if (system_task_handle != NULL) {
        vTaskResume(system_task_handle);
    }
    return 0;
}

static int cmd_quota(int argc, char **argv) {
    size_t bytes_pending = getDataLeft();
    printf("Simulated Flash Quota - Pending bytes: %u bytes (Items: %u)\n", 
           bytes_pending, bytes_pending / sizeof(float));
    return 0;
}

void register_console_commands(void) {
    ESP_ERROR_CHECK(esp_console_register_help_command());

    const esp_console_cmd_t monitor_cmd = { .command = "monitor", .help = "Return to monitoring mode", .func = &cmd_monitor };
    const esp_console_cmd_t quota_cmd = { .command = "quota", .help = "Check pending flash bytes", .func = &cmd_quota };
    
    esp_console_cmd_register(&monitor_cmd);
    esp_console_cmd_register(&quota_cmd);
}

#define BOOT_BUTTON_PIN 9

void init_boot_button(void) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BOOT_BUTTON_PIN),
        .pull_down_en = 0,
        .pull_up_en = 1,
    };
    gpio_config(&io_conf);
}

static void system_task(void *pvParameter) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(CONFIG_SENSOR_SAMPLE_INTERVAL * 1000);

    while (1) {
        float temp, hum;
        if (shtc3_get_temp_and_hum(&tempSensor, &temp, &hum) == 0) {
            ESP_LOGI(TAG, "Sampled Temp: %.2f °C | Humidity: %.2f %%", temp, hum);
            
            if (is_wifi_ready()) {
                char msg[32];
                snprintf(msg, sizeof(msg), "%.2f", temp);
                send_data_wifi(msg, strlen(msg));
            } else {
                writeToFlash(&temp, sizeof(float));
                ESP_LOGI(TAG, "Saved to simulated flash (Offline).");
            }
        }
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting Lab 3 Structured Application...");

    flash_sim_init(100); 
    simulated_wifi_init();
    init_boot_button();

    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = 8,
        .sda_io_num = 10,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));
    shtc3_init(&tempSensor, bus_handle, 0x70, (void*)vTaskDelay);

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_SIM_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    esp_console_config_t console_config = {
        .max_cmdline_args = 8,
        .max_cmdline_length = 256,
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));
    
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    register_console_commands();

    wifi_connect();
    xTaskCreate(system_task, "system_task", 4096, NULL, 5, &system_task_handle);

    // Main Loop handling Modes and Button Press
    while (1) {
        if (current_mode == MODE_MONITORING) {
            vTaskDelay(pdMS_TO_TICKS(200));

            if (gpio_get_level(BOOT_BUTTON_PIN) == 0) {
                vTaskDelay(pdMS_TO_TICKS(50)); // Debounce
                if (gpio_get_level(BOOT_BUTTON_PIN) == 0) {
                    ESP_LOGI(TAG, "BOOT button pressed! Switching to Console Mode.");
                    current_mode = MODE_CONSOLE;

                    if (system_task_handle != NULL) {
                        vTaskSuspend(system_task_handle);
                    }

                    while (gpio_get_level(BOOT_BUTTON_PIN) == 0) {
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                }
            }
        } else if (current_mode == MODE_CONSOLE) {
            wifi_disconnect();
            printf("\n--- Entrando en modo consola ---\n");
            printf("Escribe 'help' para ver los comandos. Volviendo en 10 segundos...\n");
            
            int countdown = 10;
            while (countdown > 0 && current_mode == MODE_CONSOLE) {
                char* line = linenoise("consola> ");
                if (line == NULL) { 
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    countdown--;
                    continue; 
                }
                linenoiseHistoryAdd(line);
                
                int ret;
                esp_err_t err = esp_console_run(line, &ret);
                if (err == ESP_ERR_NOT_FOUND) {
                    printf("Comando desconocido. Escribe 'help'\n");
                }
                linenoiseFree(line);
                countdown--;
            }
            
            if (current_mode == MODE_CONSOLE) {
                printf("Tiempo de consola agotado. Volviendo a monitorización...\n");
                current_mode = MODE_MONITORING;
                wifi_connect();
                if (system_task_handle != NULL) {
                    vTaskResume(system_task_handle);
                }
            }
        }
    }
}