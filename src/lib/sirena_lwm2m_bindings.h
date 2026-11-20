#ifndef SIRENA_LWM2M_BINDINGS_H
#define SIRENA_LWM2M_BINDINGS_H

#include <sirena/lwm2m/transport.h>
#include <zephyr/sys/util.h>

enum sirena_lwm2m_transport_binding {
    SIRENA_LWM2M_TRANSPORT_BINDING_NONE,
    SIRENA_LWM2M_TRANSPORT_BINDING_COAP,
    SIRENA_LWM2M_TRANSPORT_BINDING_DTLS,
};

#if IS_ENABLED(CONFIG_SIRENA_LWM2M_TRANSPORT_COAP)
extern const struct sirena_lwm2m_transport_ops sirena_lwm2m_coap_transport_ops;
int sirena_lwm2m_coap_start_engine(void);
int sirena_lwm2m_coap_stop_engine(void);
#endif

#if IS_ENABLED(CONFIG_SIRENA_LWM2M_TRANSPORT_DTLS)
extern const struct sirena_lwm2m_transport_ops sirena_lwm2m_dtls_transport_ops;
int sirena_lwm2m_dtls_start_engine(void);
int sirena_lwm2m_dtls_stop_engine(void);
#endif

int sirena_lwm2m_transport_engine_ensure(void);

#endif
