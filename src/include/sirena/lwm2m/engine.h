#ifndef SIRENA_LWM2M_ENGINE_H
#define SIRENA_LWM2M_ENGINE_H

#include <zephyr/kernel.h>

enum sirena_lwm2m_engine_operation {
    SIRENA_LWM2M_ENGINE_OPERATION_START,
    SIRENA_LWM2M_ENGINE_OPERATION_START_COAP,
    SIRENA_LWM2M_ENGINE_OPERATION_START_DTLS,
    SIRENA_LWM2M_ENGINE_OPERATION_STOP_COAP,
    SIRENA_LWM2M_ENGINE_OPERATION_STOP_DTLS,
    SIRENA_LWM2M_ENGINE_OPERATION_REGISTER,
    SIRENA_LWM2M_ENGINE_OPERATION_UPDATE,
    SIRENA_LWM2M_ENGINE_OPERATION_DEREGISTER,
    SIRENA_LWM2M_ENGINE_OPERATION_BIND,
    SIRENA_LWM2M_ENGINE_OPERATION_NOTIFY,
};

typedef int (*sirena_lwm2m_engine_handler_t)(enum sirena_lwm2m_engine_operation operation,
                                              void *payload);

typedef int (*sirena_lwm2m_engine_poll_fn)(int timeout_ms);

int sirena_lwm2m_engine_start(sirena_lwm2m_engine_handler_t handler);
int sirena_lwm2m_engine_submit(enum sirena_lwm2m_engine_operation operation, void *payload);
int sirena_lwm2m_engine_set_poll_fn(sirena_lwm2m_engine_poll_fn poll_fn);
k_tid_t sirena_lwm2m_engine_thread_id(void);

#endif
