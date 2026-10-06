idf_component_register(
    SRCS
        "main.c"
        "cfg.c"
        "cli.c"
        "rtc_ds3231.c"
        "net_eth.c"
        "net_wifi.c"
        "scope.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash esp_netif esp_event esp_wifi esp_eth
        esp_driver_gpio esp_driver_spi esp_driver_i2c
        console lwip esp_system
)
