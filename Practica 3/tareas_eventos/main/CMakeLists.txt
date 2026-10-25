idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES console esp_driver_gpio esp_driver_i2c esp_timer simulated_wifi simulated_flash shtc3)
