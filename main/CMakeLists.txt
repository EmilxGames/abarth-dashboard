idf_component_register(
    SRC_DIRS "."
             "can"
             "config"
             "data"
             "display"
             "obd"
             "storage"
             "ui"
             "ui_v2"
    INCLUDE_DIRS "."
                 "can"
                 "config"
                 "data"
                 "display"
                 "obd"
                 "storage"
                 "ui"
    REQUIRES lvgl esp_lcd esp_lcd_touch esp_lcd_touch_gt911 esp_timer nvs_flash
             driver freertos esp_system esp_wifi esp_event esp_netif lwip bt
             esp_hosted esp32_display_panel fatfs sdmmc esp_driver_sdmmc esp_hw_support
             esp_http_server app_update esp_driver_ppa esp_mm mbedtls
)
