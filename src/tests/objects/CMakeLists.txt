cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(sirena_lwm2m_objects_tests)

target_sources(app PRIVATE src/test_objects.c)

target_link_libraries(app PRIVATE sirena_lwm2m)
