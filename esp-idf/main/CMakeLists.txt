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

# The theme the firmware carries, for a device whose partition has none: the
# `face` set — the orange pet's screen on the whole panel, every state it has —
# deflated to 7 KB. theme_pack.c expands it on the first boot after a flash and
# again after any failed install.
#
# Embedded as binary rather than turned into a C array the way logo_data.h and
# the splash headers are: a pack is one file whose contents are not source, and
# a real .zip can still be read with `unzip -l default_pack.zip`. Rebuild it with
#   python3 tools/mochi_theme.py bundle themes/face -o esp-idf/main/default_pack.zip
# (--only idle,thinking,working,done,error,sleep drops the spare expressions,
# which the clawd set needs — it is 168 KB with them — and this one does not.)
target_add_binary_data(${COMPONENT_LIB} "default_pack.zip" BINARY)
