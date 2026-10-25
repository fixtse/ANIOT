idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES button console esp_driver_gpio esp_driver_i2c esp_driver_usb_serial_jtag esp_timer simulated_wifi simulated_flash shtc3)
