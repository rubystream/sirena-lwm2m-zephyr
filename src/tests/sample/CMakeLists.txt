cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(sirena_lwm2m_sample_tests)

set(SAMPLE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../samples/lwm2m_client/src)

target_sources(app PRIVATE
    src/test_sample.c
    ${SAMPLE_SOURCE_DIR}/sample_flow.c
)

target_include_directories(app PRIVATE ${SAMPLE_SOURCE_DIR})

target_link_libraries(app PRIVATE sirena_lwm2m)

zephyr_linker_sources(DATA_SECTIONS sections-ram.ld)

zephyr_iterable_section(
  NAME coap_resource_peer_service
  GROUP DATA_REGION ${XIP_ALIGN_WITH_INPUT})
