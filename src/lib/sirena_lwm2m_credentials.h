#ifndef SIRENA_LWM2M_CREDENTIALS_H
#define SIRENA_LWM2M_CREDENTIALS_H

#include <zephyr/net/tls_credentials.h>
#include <stdint.h>

int sirena_lwm2m_credentials_register(sec_tag_t tag);
void sirena_lwm2m_credentials_release(sec_tag_t tag);

#endif
