#ifndef SIRENA_LWM2M_URI_H
#define SIRENA_LWM2M_URI_H

#include <zephyr/net/net_ip.h>
#include <stdint.h>

enum sirena_lwm2m_uri_scheme {
    SIRENA_LWM2M_URI_SCHEME_COAP,
    SIRENA_LWM2M_URI_SCHEME_COAPS,
    SIRENA_LWM2M_URI_SCHEME_MQTT,
    SIRENA_LWM2M_URI_SCHEME_MQTTS,
};

int sirena_lwm2m_uri_scheme(const char *uri, enum sirena_lwm2m_uri_scheme *scheme);
int sirena_lwm2m_uri_server_address(struct net_sockaddr_storage *storage,
                                    enum sirena_lwm2m_uri_scheme scheme);

#endif
