#include "simulated_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SIM_WIFI";
ESP_EVENT_DEFINE_BASE(WIFI_SIM_EVENT);

static bool s_connected = false;
static bool s_ip_acquired = false;

static void wifi_simulation_task(void *pvParameters) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(15000)); // Simulate connection lifecycle
        if (s_connected && !s_ip_acquired) {
            s_ip_acquired = true;
            ESP_LOGI(TAG, "IP Acquired!");
            esp_event_post(WIFI_SIM_EVENT, WIFI_SIM_EVENT_IP_ACQUIRED, NULL, 0, portMAX_DELAY);
        }
    }
}

void simulated_wifi_init(void) {
    xTaskCreate(wifi_simulation_task, "wifi_sim_task", 2048, NULL, 5, NULL);
}

esp_err_t wifi_connect(void) {
    ESP_LOGI(TAG, "Connecting to simulated Wi-Fi...");
    vTaskDelay(pdMS_TO_TICKS(1000)); // Simulate connection latency
    s_connected = true;
    s_ip_acquired = false;
    esp_event_post(WIFI_SIM_EVENT, WIFI_SIM_EVENT_CONNECTED, NULL, 0, portMAX_DELAY);
    return ESP_OK;
}

esp_err_t wifi_disconnect(void) {
    ESP_LOGI(TAG, "Disconnected from simulated Wi-Fi.");
    s_connected = false;
    s_ip_acquired = false;
    esp_event_post(WIFI_SIM_EVENT, WIFI_SIM_EVENT_DISCONNECTED, NULL, 0, portMAX_DELAY);
    return ESP_OK;
}

esp_err_t send_data_wifi(void* data, size_t size) {
    if (!s_ip_acquired) {
        ESP_LOGW(TAG, "Send failed: No IP address assigned!");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Wi-Fi TX -> Data: %s (Size: %u bytes)", (char*)data, size);
    return ESP_OK;
}

bool is_wifi_ready(void) {
    return s_ip_acquired;
}