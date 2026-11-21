cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(sirena_lwm2m_mqtt_tests)

target_sources(app PRIVATE src/test_mqtt.c)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../lib)

target_link_libraries(app PRIVATE sirena_lwm2m)
