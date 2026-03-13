idf_component_register(
    SRCS "main.c" "wifi_manager.c"
    INCLUDE_DIRS "."
    REQUIRES rt_claw console esp_driver_uart esp_wifi esp_netif nvs_flash
)
