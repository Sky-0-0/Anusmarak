idf_component_register(
    SRCS "Utilities.cpp"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "Status.html"
    REQUIRES littlefs esp_timer esp_driver_gpio esp_driver_i2c esp_driver_spi esp_adc esp_http_server esp_event esp_driver_ledc esp_driver_tsens espressif__led_strip app_update
    PRIV_REQUIRES esp_wifi esp_netif espressif__mdns
)
