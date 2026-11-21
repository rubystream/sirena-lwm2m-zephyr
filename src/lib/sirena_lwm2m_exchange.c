#include "sirena_lwm2m_exchange.h"
#include "sirena_lwm2m_coap_endpoint.h"
#include "sirena_lwm2m_link_format.h"
#include <sirena/lwm2m/object_registry.h>
#include <sirena/lwm2m/objects.h>
#include <sirena/lwm2m/transport.h>
#include <zephyr/kernel.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define SIRENA_LWM2M_SECURITY_OBJECT_ID 0U
#define SIRENA_LWM2M_SERVER_OBJECT_ID 1U
#define SIRENA_LWM2M_DEVICE_OBJECT_ID 3U

#define SIRENA_LWM2M_SERVER_URI_RESOURCE 0U
#define SIRENA_LWM2M_SERVER_LIFETIME_RESOURCE 1U

#define SIRENA_LWM2M_COAP_REGISTER_PATH "rd"
#define SIRENA_LWM2M_COAP_LWM2M_VERSION "1.2"
#define SIRENA_LWM2M_COAP_BINDING_MODE "U"

int sirena_lwm2m_exchange_status(int16_t code)
{
    if (code < 0) {
        return code;
    }

    switch (code) {
    case COAP_RESPONSE_CODE_CREATED:
    case COAP_RESPONSE_CODE_DELETED:
    case COAP_RESPONSE_CODE_CHANGED:
    case COAP_RESPONSE_CODE_CONTENT:
        return 0;
    case COAP_RESPONSE_CODE_BAD_REQUEST:
        return -EBADMSG;
    case COAP_RESPONSE_CODE_FORBIDDEN:
        return -EACCES;
    case COAP_RESPONSE_CODE_NOT_FOUND:
        return -ENOENT;
    case COAP_RESPONSE_CODE_PRECONDITION_FAILED:
        return -EPROTO;
    case COAP_RESPONSE_CODE_BAD_GATEWAY:
        return -EHOSTUNREACH;
    default:
        return -EIO;
    }
}

int sirena_lwm2m_exchange_request(enum sirena_lwm2m_uri_scheme scheme, const char *path,
                                  enum coap_method method, bool content_format_present,
                                  uint16_t content_format, const void *payload,
                                  size_t payload_length, char *location, size_t location_capacity)
{
    struct net_sockaddr_storage storage;
    struct sirena_lwm2m_coap_response response;
    char response_location[CONFIG_SIRENA_LWM2M_LOCATION_PATH_MAX + 1U];
    int rc;

    rc = sirena_lwm2m_exchange_server_address(&storage, scheme);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_coap_endpoint_request(net_sad(&storage), sizeof(struct net_sockaddr_in),
                                             path, method, content_format_present, content_format,
                                             payload, payload_length, &response, response_location,
                                             sizeof(response_location));
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_exchange_status(response.code);
    if (rc != 0) {
        return rc;
    }

    if (location != NULL) {
        size_t length = strlen(response_location);

        if (length >= location_capacity) {
            return -ENOMEM;
        }

        memcpy(location, response_location, length + 1U);
    }

    return 0;
}

static int server_lifetime(int64_t *lifetime)
{
    return sirena_lwm2m_get_integer(SIRENA_LWM2M_SERVER_OBJECT_ID, 0U,
                                    SIRENA_LWM2M_SERVER_LIFETIME_RESOURCE, lifetime);
}

int sirena_lwm2m_exchange_register(char *path, size_t path_capacity, char *payload,
                                   size_t payload_capacity, size_t *payload_length)
{
    const char *endpoint_name = sirena_lwm2m_transport_endpoint_name();
    int64_t lifetime;
    int rc;
    int written;

    if (endpoint_name[0] == '\0') {
        return -EINVAL;
    }

    rc = server_lifetime(&lifetime);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_link_format_objects(payload, payload_capacity, payload_length);
    if (rc != 0) {
        return rc;
    }

    written = snprintf(path, path_capacity, "%s?ep=%s&lt=%lld&lwm2m=%s&b=%s",
                       SIRENA_LWM2M_COAP_REGISTER_PATH, endpoint_name,
                       (long long)lifetime, SIRENA_LWM2M_COAP_LWM2M_VERSION,
                       SIRENA_LWM2M_COAP_BINDING_MODE);
    if (written < 0 || (size_t)written >= path_capacity) {
        return -ENOMEM;
    }

    return 0;
}

int sirena_lwm2m_exchange_update(const char *location, char *path, size_t path_capacity)
{
    int64_t lifetime;
    int rc;
    int written;

    rc = server_lifetime(&lifetime);
    if (rc != 0) {
        return rc;
    }

    written = snprintf(path, path_capacity, "%s?lt=%lld&b=%s", location, (long long)lifetime,
                       SIRENA_LWM2M_COAP_BINDING_MODE);
    if (written < 0 || (size_t)written >= path_capacity) {
        return -ENOMEM;
    }

    return 0;
}

int sirena_lwm2m_exchange_server_address(struct net_sockaddr_storage *storage,
                                         enum sirena_lwm2m_uri_scheme scheme)
{
    return sirena_lwm2m_uri_server_address(storage, scheme);
}
