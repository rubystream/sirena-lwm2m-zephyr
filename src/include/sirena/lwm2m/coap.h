#ifndef SIRENA_LWM2M_COAP_H
#define SIRENA_LWM2M_COAP_H

#include <stddef.h>

int sirena_lwm2m_coap_set_endpoint_name(const char *name);
int sirena_lwm2m_coap_start(void);
int sirena_lwm2m_coap_get_location_path(char *buffer, size_t *length);

#endif
