#ifndef SIMULATED_WIFI_H
#define SIMULATED_WIFI_H

#include "esp_err.h"
#include "esp_event.h"

// Eventos de conexión WiFi e IP disponibles para los componentes consumidores.
ESP_EVENT_DECLARE_BASE(WIFI_SIM_EVENT);

enum {
    WIFI_SIM_EVENT_CONNECTED,
    WIFI_SIM_EVENT_IP_ACQUIRED,
    WIFI_SIM_EVENT_DISCONNECTED
};

void simulated_wifi_init(void);
esp_err_t wifi_connect(void);
esp_err_t wifi_disconnect(void);
esp_err_t send_data_wifi(void* data, size_t size);
bool is_wifi_ready(void);

#endif
