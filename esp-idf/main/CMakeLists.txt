idf_component_register(
  SRCS "main.cpp" "display.cpp" "ble_cli.c" "anim.cpp"
  INCLUDE_DIRS "."
  REQUIRES esp_lcd
            esp_driver_spi
            esp_driver_gpio
            esp_driver_ledc
            esp_driver_uart
            esp_driver_usb_serial_jtag
            bt
            littlefs
            esp_wifi
            esp_netif
            esp_http_server
            nvs_flash
            esp_event
            esp_rom
            lwip
)
