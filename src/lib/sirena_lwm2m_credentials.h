#ifndef SIRENA_LWM2M_CREDENTIALS_H
#define SIRENA_LWM2M_CREDENTIALS_H

#include <zephyr/net/tls_credentials.h>
#include <stdint.h>

int sirena_lwm2m_credentials_register_one(sec_tag_t tag);
int sirena_lwm2m_credentials_register_mode(sec_tag_t tag, int64_t security_mode);
void sirena_lwm2m_credentials_release(sec_tag_t tag);

#define SIRENA_LWM2M_CREDENTIALS_REGISTER_1(tag) \
    sirena_lwm2m_credentials_register_one(tag)
#define SIRENA_LWM2M_CREDENTIALS_REGISTER_2(tag, mode) \
    sirena_lwm2m_credentials_register_mode(tag, mode)
#define SIRENA_LWM2M_CREDENTIALS_REGISTER_GET(_1, _2, NAME, ...) NAME
#define sirena_lwm2m_credentials_register(...)                                             \
    SIRENA_LWM2M_CREDENTIALS_REGISTER_GET(__VA_ARGS__,                                    \
                                         SIRENA_LWM2M_CREDENTIALS_REGISTER_2,             \
                                         SIRENA_LWM2M_CREDENTIALS_REGISTER_1)             \
    (__VA_ARGS__)

#endif
