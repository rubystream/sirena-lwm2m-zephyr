#ifndef SIRENA_LWM2M_COAP_ENDPOINT_H
#define SIRENA_LWM2M_COAP_ENDPOINT_H

#include <zephyr/net/coap.h>
#include <zephyr/net/net_ip.h>
#include <stdint.h>
#include <stddef.h>

struct sirena_lwm2m_coap_response {
    uint8_t code;
    uint16_t content_format;
    const void *payload;
    size_t payload_length;
};

int sirena_lwm2m_coap_endpoint_start(int socket);
void sirena_lwm2m_coap_endpoint_stop(void);
int sirena_lwm2m_coap_endpoint_parse_path(const struct coap_packet *packet, uint16_t *path,
                                           size_t capacity, size_t *count);
int sirena_lwm2m_coap_endpoint_poll(int timeout_ms);
int sirena_lwm2m_coap_endpoint_request(const struct net_sockaddr *server,
                                       net_socklen_t server_length, const char *path,
                                       enum coap_method method, bool content_format_present,
                                       uint16_t content_format, const void *payload,
                                       size_t payload_length,
                                       struct sirena_lwm2m_coap_response *response,
                                       char *location, size_t location_capacity);
int sirena_lwm2m_coap_endpoint_notify(uint16_t object_id, uint16_t instance_id,
                                      uint16_t resource_id);
void sirena_lwm2m_coap_endpoint_enforce_pmax(void);

#endif
