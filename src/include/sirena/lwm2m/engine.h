#ifndef SIRENA_LWM2M_ENGINE_H
#define SIRENA_LWM2M_ENGINE_H

enum sirena_lwm2m_engine_operation {
    SIRENA_LWM2M_ENGINE_OPERATION_REGISTER,
    SIRENA_LWM2M_ENGINE_OPERATION_UPDATE,
    SIRENA_LWM2M_ENGINE_OPERATION_DEREGISTER,
};

typedef int (*sirena_lwm2m_engine_handler_t)(enum sirena_lwm2m_engine_operation operation);

int sirena_lwm2m_engine_start(sirena_lwm2m_engine_handler_t handler);
int sirena_lwm2m_engine_submit(enum sirena_lwm2m_engine_operation operation);

#endif
