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
bool sirena_lwm2m_coap_endpoint_is_started(void);
int sirena_lwm2m_coap_endpoint_poll(int timeout_ms);
int sirena_lwm2m_coap_endpoint_request(const struct net_sockaddr *server,
                                       net_socklen_t server_length, const char *path,
                                       enum coap_method method, uint16_t content_format,
                                       const void *payload, size_t payload_length,
                                       struct sirena_lwm2m_coap_response *response,
                                       char *location, size_t location_capacity);
int sirena_lwm2m_coap_endpoint_notify(uint16_t object_id, uint16_t instance_id,
                                      uint16_t resource_id);
size_t sirena_lwm2m_coap_endpoint_observation_count(void);

#endif
