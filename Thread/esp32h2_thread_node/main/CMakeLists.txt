idf_component_register(
    SRCS "esp_ot_cli.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_event
        esp_netif
        vfs
        nvs_flash
        driver
        freertos
        openthread
        led_strip
        esp_ot_cli_extension
)