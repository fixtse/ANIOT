#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_console.h"
#include "button.h"
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
static TaskHandle_t system_task_handle = NULL; // Se suspende durante el modo consola para detener las lecturas.
static TaskHandle_t main_task_handle;
static esp_console_repl_t *console_repl;

static void button_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    // El componente del botón notifica la pulsación; la tarea principal realiza el cambio de modo.
    if (current_mode == MODE_MONITORING) {
        xTaskNotifyGive(main_task_handle);
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_SIM_EVENT) {
        switch (event_id) {
            case WIFI_SIM_EVENT_CONNECTED:
                ESP_LOGI(TAG, "Event: Wi-Fi Connected");
                break;
            case WIFI_SIM_EVENT_IP_ACQUIRED:
                ESP_LOGI(TAG, "Event: IP Acquired! Flushing flash buffer...");
                // Al obtener IP, se vacía en orden la cola circular de medidas pendientes.
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
    const esp_console_cmd_t monitor_cmd = { .command = "monitor", .help = "Return to monitoring mode", .func = &cmd_monitor };
    const esp_console_cmd_t quota_cmd = { .command = "quota", .help = "Check pending flash bytes", .func = &cmd_quota };
    
    ESP_ERROR_CHECK(esp_console_cmd_register(&monitor_cmd));
    ESP_ERROR_CHECK(esp_console_cmd_register(&quota_cmd));
}

static void start_console(void) {
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "consola> ";
    repl_config.max_cmdline_length = 256;
    repl_config.max_cmdline_args = 8;

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &console_repl));
#else
    ESP_ERROR_CHECK(esp_console_new_repl_stdio(&repl_config, &console_repl));
#endif
    ESP_ERROR_CHECK(esp_console_register_help_command());
    register_console_commands();
    ESP_ERROR_CHECK(esp_console_start_repl(console_repl));
}

static void stop_console(void) {
    if (console_repl != NULL) {
        ESP_ERROR_CHECK(esp_console_stop_repl(console_repl));
        console_repl = NULL;
    }
}

#define BOOT_BUTTON_PIN 9

static void system_task(void *pvParameter) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    // El periodo de muestreo se selecciona desde menuconfig.
    const TickType_t xFrequency = pdMS_TO_TICKS(CONFIG_SENSOR_SAMPLE_INTERVAL * 1000);

    while (1) {
        float temp, hum;
        if (shtc3_get_temp_and_hum(&tempSensor, &temp, &hum) == 0) {
            ESP_LOGI(TAG, "Sampled Temp: %.2f °C | Humidity: %.2f %%", temp, hum);
            
            // Sin IP, cada temperatura se conserva en el componente de flash simulado.
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

    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    wifi_connect();
    xTaskCreate(system_task, "system_task", 4096, NULL, 5, &system_task_handle);

    main_task_handle = xTaskGetCurrentTaskHandle();
    ESP_ERROR_CHECK(esp_event_handler_register(BUTTON_EVENT, BUTTON_PRESSED,
                                               button_event_handler, NULL));
    ESP_ERROR_CHECK(button_init(BOOT_BUTTON_PIN, CONFIG_NBUTTON));
    printf("Monitoring: press the BOOT button to enter console mode.\n");

    // La aplicación alterna entre monitorización y consola al recibir el evento del botón.
    while (1) {
        if (current_mode == MODE_MONITORING) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            ESP_LOGI(TAG, "Button event received! Switching to Console Mode.");
            current_mode = MODE_CONSOLE;
            if (system_task_handle != NULL) {
                vTaskSuspend(system_task_handle);
            }
            wifi_disconnect();
            printf("\nEntrando en modo consola\n");
            printf("Escribe 'help' para ver los comandos.\n");
            start_console();
            int64_t deadline = esp_timer_get_time() + 10 * 1000000LL;
            while (current_mode == MODE_CONSOLE && esp_timer_get_time() < deadline) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            stop_console();
            if (current_mode == MODE_CONSOLE) {
                printf("\nTiempo de consola agotado.\n");
                cmd_monitor(0, NULL);
            }
        } else if (current_mode == MODE_CONSOLE) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}
