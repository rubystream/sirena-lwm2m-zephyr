#ifndef SIRENA_LWM2M_TRANSPORT_H
#define SIRENA_LWM2M_TRANSPORT_H

#include <stdint.h>

struct sirena_lwm2m_transport_ops {
    int (*register_client)(void);
    int (*update)(void);
    int (*deregister)(void);
    int (*poll)(int timeout_ms);
    int (*notify)(uint16_t object_id, uint16_t instance_id, uint16_t resource_id);
};

int sirena_lwm2m_transport_bind(const struct sirena_lwm2m_transport_ops *ops);
int sirena_lwm2m_transport_register(void);
int sirena_lwm2m_transport_update(void);
int sirena_lwm2m_transport_deregister(void);

int sirena_lwm2m_transport_set_endpoint_name(const char *name);
const char *sirena_lwm2m_transport_endpoint_name(void);
int sirena_lwm2m_transport_start(void);

#endif
