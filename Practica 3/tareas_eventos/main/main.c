#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_console.h"
#include "button.h"
#include "driver/i2c_master.h"
#include "sdkconfig.h"
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#endif

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
    ESP_ERROR_CHECK(esp_console_register_help_command());

    const esp_console_cmd_t monitor_cmd = { .command = "monitor", .help = "Return to monitoring mode", .func = &cmd_monitor };
    const esp_console_cmd_t quota_cmd = { .command = "quota", .help = "Check pending flash bytes", .func = &cmd_quota };
    
    esp_console_cmd_register(&monitor_cmd);
    esp_console_cmd_register(&quota_cmd);
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

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    // Buffer incoming USB bytes with the interrupt-driven console driver.
    usb_serial_jtag_driver_config_t usb_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_config));
    usb_serial_jtag_vfs_use_driver();
#endif

    // La lectura no bloqueante permite atender a la vez la consola y los eventos.
    int stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, stdin_flags | O_NONBLOCK);

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

    main_task_handle = xTaskGetCurrentTaskHandle();
    ESP_ERROR_CHECK(esp_event_handler_register(BUTTON_EVENT, BUTTON_PRESSED,
                                               button_event_handler, NULL));
    ESP_ERROR_CHECK(button_init(BOOT_BUTTON_PIN, CONFIG_NBUTTON));
    printf("Monitoring: send 'b' over serial to simulate a BOOT button press.\n");

    // La aplicación alterna entre monitorización y consola al recibir el evento del botón.
    while (1) {
        if (current_mode == MODE_MONITORING) {
            // Lee el terminal sin bloquear; 'b' permite simular una pulsación del botón.
            char input[64];
            ssize_t received = read(STDIN_FILENO, input, sizeof(input));
            for (ssize_t i = 0; i < received; ++i) {
                if (input[i] == 'b' || input[i] == 'B') {
                    ESP_ERROR_CHECK(button_simulate_press());
                    break;
                }
            }
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20)) == 0) {
                continue;
            }
            ESP_LOGI(TAG, "Button event received! Switching to Console Mode.");
            current_mode = MODE_CONSOLE;
            if (system_task_handle != NULL) {
                vTaskSuspend(system_task_handle);
            }
        } else if (current_mode == MODE_CONSOLE) {
            // La consola detiene el muestreo, desconecta WiFi y limita su uso a 10 segundos.
            wifi_disconnect();
            printf("\nEntrando en modo consola\n");
            printf("Escribe 'help' para ver los comandos.\n");

            int64_t deadline = esp_timer_get_time() + 10 * 1000000LL;
            char line[256];
            size_t length = 0;
            printf("consola> ");
            while (current_mode == MODE_CONSOLE) {
                int64_t remaining_us = deadline - esp_timer_get_time();
                if (remaining_us <= 0) {
                    break;
                }

                char c;
                if (read(STDIN_FILENO, &c, 1) == 1) {
                    if (c == '\n' || c == '\r') {
                        printf("\n");
                        line[length] = '\0';
                        if (length > 0) {
                            int ret;
                            esp_err_t err = esp_console_run(line, &ret);
                            if (err == ESP_ERR_NOT_FOUND) {
                                printf("Comando desconocido. Escribe 'help'\n");
                            }
                        }
                        length = 0;
                        if (current_mode == MODE_CONSOLE) {
                            printf("consola> ");
                        }
                    } else if (c == '\b' || c == 127) {
                        if (length > 0) {
                            length--;
                            printf("\b \b");
                        }
                    } else if (c >= 32 && c < 127 && length < sizeof(line) - 1) {
                        line[length++] = c;
                        putchar(c);
                    }
                } else {
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
            }
            if (current_mode == MODE_CONSOLE) {
                printf("\nTiempo de consola agotado.\n");
                cmd_monitor(0, NULL);
            }
        }
    }
}
