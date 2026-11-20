cmake_minimum_required(VERSION 3.20.0)

zephyr_library_named(sirena_lwm2m)

zephyr_include_directories(src/include)

if(CONFIG_SIRENA_LWM2M)
    zephyr_library_sources(
        src/lib/sirena_lwm2m_object_registry.c
        src/lib/sirena_lwm2m_codec.c
        src/lib/sirena_lwm2m_objects.c
        src/lib/sirena_lwm2m_engine.c
        src/lib/sirena_lwm2m_transport.c
    )
endif()

if(CONFIG_SIRENA_LWM2M_TRANSPORT_COAP)
    zephyr_library_sources(
        src/lib/sirena_lwm2m_coap.c
    )
endif()
