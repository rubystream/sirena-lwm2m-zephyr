#ifndef SIRENA_LWM2M_DTLS_H
#define SIRENA_LWM2M_DTLS_H

#include <stddef.h>

int sirena_lwm2m_dtls_start(void);
int sirena_lwm2m_dtls_stop(void);
int sirena_lwm2m_dtls_get_location_path(char *buffer, size_t *length);

#endif
