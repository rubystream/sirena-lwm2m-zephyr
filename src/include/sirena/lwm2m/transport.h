#ifndef SIRENA_LWM2M_TRANSPORT_H
#define SIRENA_LWM2M_TRANSPORT_H

struct sirena_lwm2m_transport_ops {
    int (*register_client)(void);
    int (*update)(void);
    int (*deregister)(void);
};

int sirena_lwm2m_transport_bind(const struct sirena_lwm2m_transport_ops *ops);
int sirena_lwm2m_transport_register(void);
int sirena_lwm2m_transport_update(void);
int sirena_lwm2m_transport_deregister(void);

#endif
