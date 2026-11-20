#ifndef SIRENA_LWM2M_COAP_CLIENT_H
#define SIRENA_LWM2M_COAP_CLIENT_H

#include <zephyr/net/coap_client.h>

int sirena_lwm2m_coap_client_get(struct coap_client **client);

#endif
