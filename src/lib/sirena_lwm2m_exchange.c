#include "sirena_lwm2m_exchange.h"
#include "sirena_lwm2m_coap_endpoint.h"
#include <sirena/lwm2m/object_registry.h>
#include <sirena/lwm2m/objects.h>
#include <sirena/lwm2m/transport.h>
#include <zephyr/kernel.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIRENA_LWM2M_SECURITY_OBJECT_ID 0U
#define SIRENA_LWM2M_SERVER_OBJECT_ID 1U
#define SIRENA_LWM2M_DEVICE_OBJECT_ID 3U

#define SIRENA_LWM2M_SERVER_URI_RESOURCE 0U
#define SIRENA_LWM2M_SERVER_LIFETIME_RESOURCE 1U

#define SIRENA_LWM2M_COAP_SCHEME "coap://"
#define SIRENA_LWM2M_COAPS_SCHEME "coaps://"
#define SIRENA_LWM2M_COAP_DEFAULT_PORT 5683
#define SIRENA_LWM2M_COAPS_DEFAULT_PORT 5684
#define SIRENA_LWM2M_COAP_REGISTER_PATH "rd"
#define SIRENA_LWM2M_COAP_LWM2M_VERSION "1.2"
#define SIRENA_LWM2M_COAP_BINDING_MODE "U"

struct sirena_lwm2m_exchange_payload {
    char *buffer;
    size_t capacity;
    size_t length;
    bool server_present;
    bool device_present;
};

static char server_uri[CONFIG_SIRENA_LWM2M_SERVER_URI_MAX + 1U];

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

int sirena_lwm2m_exchange_request(enum sirena_lwm2m_exchange_scheme scheme, const char *path,
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

static int payload_append(uint16_t object_id, uint16_t instance_id, void *user_data)
{
    struct sirena_lwm2m_exchange_payload *payload = user_data;
    int written;

    if (object_id == SIRENA_LWM2M_SECURITY_OBJECT_ID) {
        return 0;
    }

    written = snprintf(payload->buffer + payload->length, payload->capacity - payload->length,
                       "%s</%u/%u>", payload->length > 0U ? "," : "", object_id, instance_id);
    if (written < 0 || (size_t)written >= payload->capacity - payload->length) {
        return -ENOMEM;
    }

    payload->length += (size_t)written;

    if (object_id == SIRENA_LWM2M_SERVER_OBJECT_ID) {
        payload->server_present = true;
    } else if (object_id == SIRENA_LWM2M_DEVICE_OBJECT_ID) {
        payload->device_present = true;
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
    struct sirena_lwm2m_exchange_payload context = {
        .buffer = payload,
        .capacity = payload_capacity,
        .length = 0U,
    };
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

    rc = sirena_lwm2m_foreach_instance(payload_append, &context);
    if (rc != 0) {
        return rc;
    }

    if (!context.server_present || !context.device_present) {
        return -EINVAL;
    }

    written = snprintf(path, path_capacity, "%s?ep=%s&lt=%lld&lwm2m=%s&b=%s",
                       SIRENA_LWM2M_COAP_REGISTER_PATH, endpoint_name,
                       (long long)lifetime, SIRENA_LWM2M_COAP_LWM2M_VERSION,
                       SIRENA_LWM2M_COAP_BINDING_MODE);
    if (written < 0 || (size_t)written >= path_capacity) {
        return -ENOMEM;
    }

    *payload_length = context.length;

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
                                         enum sirena_lwm2m_exchange_scheme scheme)
{
    struct net_sockaddr_in *address = (struct net_sockaddr_in *)storage;
    size_t length = sizeof(server_uri);
    unsigned long port;
    char *host;
    char *separator;
    int rc;

    rc = sirena_lwm2m_get_string(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                 SIRENA_LWM2M_SERVER_URI_RESOURCE, server_uri, &length);
    if (rc != 0) {
        return rc;
    }

    if (scheme == SIRENA_LWM2M_EXCHANGE_SCHEME_COAP) {
        if (strncmp(server_uri, SIRENA_LWM2M_COAPS_SCHEME,
                    strlen(SIRENA_LWM2M_COAPS_SCHEME)) == 0) {
            return -ENOTSUP;
        }

        if (strncmp(server_uri, SIRENA_LWM2M_COAP_SCHEME,
                    strlen(SIRENA_LWM2M_COAP_SCHEME)) != 0) {
            return -EINVAL;
        }

        host = server_uri + strlen(SIRENA_LWM2M_COAP_SCHEME);
        port = SIRENA_LWM2M_COAP_DEFAULT_PORT;
    } else {
        if (strncmp(server_uri, SIRENA_LWM2M_COAPS_SCHEME,
                    strlen(SIRENA_LWM2M_COAPS_SCHEME)) != 0) {
            return -EINVAL;
        }

        host = server_uri + strlen(SIRENA_LWM2M_COAPS_SCHEME);
        port = SIRENA_LWM2M_COAPS_DEFAULT_PORT;
    }

    separator = strrchr(host, ':');
    if (separator != NULL) {
        *separator = '\0';
        port = strtoul(separator + 1, NULL, 10);
        if (port == 0UL || port > 65535UL) {
            return -EINVAL;
        }
    }

    memset(storage, 0, sizeof(*storage));
    address->sin_family = NET_AF_INET;
    address->sin_port = net_htons((uint16_t)port);

    if (zsock_inet_pton(NET_AF_INET, host, &address->sin_addr) != 1) {
        return -ENOTSUP;
    }

    return 0;
}
