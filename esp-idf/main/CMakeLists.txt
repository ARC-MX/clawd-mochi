idf_component_register(
  SRCS "theme_pack.c" "settings.c" "transport_icon.cpp" "main.cpp" "display.cpp" "ble_cli.c" "anim.cpp"
  INCLUDE_DIRS "."
  REQUIRES esp_lcd
            esp_driver_spi
            esp_driver_gpio
            esp_driver_ledc
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

# The theme the firmware carries, for a device whose partition has none: the six
# states the hooks drive, deflated, 168 KB. theme_pack.c expands it on the first
# boot after a flash and again after any failed install.
#
# Embedded as binary rather than turned into a C array the way logo_data.h and
# the splash headers are: 168 KB of bytes would be a ~600 KB source file, and a
# real .zip can still be read with `unzip -l default_pack.zip`. Rebuild it with
#   python3 tools/mochi_theme.py bundle themes/clawd \
#       -o esp-idf/main/default_pack.zip --only idle,thinking,working,done,error,sleep
target_add_binary_data(${COMPONENT_LIB} "default_pack.zip" BINARY)
