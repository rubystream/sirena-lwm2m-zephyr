cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(sirena_lwm2m_dtls_tests)

target_sources(app PRIVATE src/test_dtls.c)

target_link_libraries(app PRIVATE sirena_lwm2m)

if(CONFIG_MBEDTLS_BUILTIN)
    target_link_libraries(app PRIVATE mbedtls_iface)
endif()

zephyr_linker_sources(DATA_SECTIONS sections-ram.ld)

zephyr_iterable_section(
  NAME coap_resource_peer_service
  GROUP DATA_REGION ${XIP_ALIGN_WITH_INPUT})
