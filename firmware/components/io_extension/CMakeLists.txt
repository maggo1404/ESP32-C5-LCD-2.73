idf_component_register(SRCS io_extension.c
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_netif esp_driver_i2c esp_driver_gpio)
# vendor code: silence its unused static helper
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function)
