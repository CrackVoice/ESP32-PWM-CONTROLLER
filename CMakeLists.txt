# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ESP32C6-PWM-motor)

set(REQUIRES
    esp_wifi
    esp_event
    esp_log
    nvs_flash
    esp_http_server
    driver
    esp_netif
)

idf_component_set_property(main REQUIRES ${REQUIRES})
