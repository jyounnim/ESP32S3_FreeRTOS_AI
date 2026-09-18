# gpio/i2c drivers live in separate components since IDF v5.2 (required on v6+)
set(priv_reqs driver)
if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "5.2")
    list(APPEND priv_reqs esp_driver_i2c)
endif()

idf_component_register(
    SRCS "main.c" "oled_status.c"
    PRIV_REQUIRES ${priv_reqs}
    INCLUDE_DIRS "."
)
