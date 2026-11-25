idf_component_register(
    SRCS
        main.c
        board.c
        battery.c
        lvgl_port.c
        rx_11p.c
        sd_log.c
        settings.c
        usb_out.c
        buttons.c
        ui.c
        ui_fonts.c
        fonts/ui_font_montserrat_12.c
        fonts/ui_font_montserrat_14.c
        fonts/ui_font_montserrat_20.c
        fonts/ui_font_montserrat_28.c
        ui_lights.c
    INCLUDE_DIRS "."
    LDFRAGMENTS "linker.lf"
    REQUIRES
        its io_extension esp_lcd_ili9488
        esp_wifi esp_event esp_timer esp_lcd driver esp_driver_gpio esp_driver_spi esp_driver_i2c
        esp_driver_usb_serial_jtag fatfs sdmmc vfs nvs_flash esp_hw_support heap
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
