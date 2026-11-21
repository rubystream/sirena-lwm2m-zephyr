#ifndef SIRENA_LWM2M_EXCHANGE_H
#define SIRENA_LWM2M_EXCHANGE_H

#include "sirena_lwm2m_uri.h"
#include <zephyr/net/coap.h>
#include <zephyr/net/net_ip.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int sirena_lwm2m_exchange_status(int16_t result_code);
int sirena_lwm2m_exchange_request(enum sirena_lwm2m_uri_scheme scheme, const char *path,
                                  enum coap_method method, bool content_format_present,
                                  uint16_t content_format, const void *payload,
                                  size_t payload_length, char *location, size_t location_capacity);
int sirena_lwm2m_exchange_register(char *path, size_t path_capacity, char *payload,
                                   size_t payload_capacity, size_t *payload_length);
int sirena_lwm2m_exchange_update(const char *location, char *path, size_t path_capacity);
int sirena_lwm2m_exchange_server_address(struct net_sockaddr_storage *storage,
                                         enum sirena_lwm2m_uri_scheme scheme);

#endif
