#include "sirena_lwm2m_uri.h"
#include <sirena/lwm2m/objects.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define SIRENA_LWM2M_SECURITY_OBJECT_ID 0U
#define SIRENA_LWM2M_SECURITY_URI_RESOURCE 0U

#define SIRENA_LWM2M_URI_COAP "coap://"
#define SIRENA_LWM2M_URI_COAPS "coaps://"
#define SIRENA_LWM2M_URI_MQTT "mqtt://"
#define SIRENA_LWM2M_URI_MQTTS "mqtts://"

#define SIRENA_LWM2M_URI_COAP_PORT 5683
#define SIRENA_LWM2M_URI_COAPS_PORT 5684
#define SIRENA_LWM2M_URI_MQTT_PORT 1883
#define SIRENA_LWM2M_URI_MQTTS_PORT 8883

static char server_uri[CONFIG_SIRENA_LWM2M_SERVER_URI_MAX + 1U];

static bool uri_has_scheme(const char *uri, const char *scheme)
{
    return strncmp(uri, scheme, strlen(scheme)) == 0;
}

int sirena_lwm2m_uri_scheme(const char *uri, enum sirena_lwm2m_uri_scheme *scheme)
{
    if (uri == NULL || scheme == NULL) {
        return -EINVAL;
    }

    if (uri_has_scheme(uri, SIRENA_LWM2M_URI_COAPS)) {
        *scheme = SIRENA_LWM2M_URI_SCHEME_COAPS;
        return 0;
    }

    if (uri_has_scheme(uri, SIRENA_LWM2M_URI_COAP)) {
        *scheme = SIRENA_LWM2M_URI_SCHEME_COAP;
        return 0;
    }

    if (uri_has_scheme(uri, SIRENA_LWM2M_URI_MQTTS)) {
        *scheme = SIRENA_LWM2M_URI_SCHEME_MQTTS;
        return 0;
    }

    if (uri_has_scheme(uri, SIRENA_LWM2M_URI_MQTT)) {
        *scheme = SIRENA_LWM2M_URI_SCHEME_MQTT;
        return 0;
    }

    return -EINVAL;
}

int sirena_lwm2m_uri_server_address(struct net_sockaddr_storage *storage,
                                    enum sirena_lwm2m_uri_scheme scheme)
{
    struct net_sockaddr_in *address = (struct net_sockaddr_in *)storage;
    const char *prefix;
    unsigned long port;
    char *host;
    char *separator;
    size_t length = sizeof(server_uri);
    int rc;

    rc = sirena_lwm2m_get_string(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                 SIRENA_LWM2M_SECURITY_URI_RESOURCE, server_uri, &length);
    if (rc != 0) {
        return rc;
    }

    switch (scheme) {
    case SIRENA_LWM2M_URI_SCHEME_COAP:
        if (uri_has_scheme(server_uri, SIRENA_LWM2M_URI_COAPS)) {
            return -ENOTSUP;
        }

        prefix = SIRENA_LWM2M_URI_COAP;
        port = SIRENA_LWM2M_URI_COAP_PORT;
        break;
    case SIRENA_LWM2M_URI_SCHEME_COAPS:
        prefix = SIRENA_LWM2M_URI_COAPS;
        port = SIRENA_LWM2M_URI_COAPS_PORT;
        break;
    case SIRENA_LWM2M_URI_SCHEME_MQTT:
        prefix = SIRENA_LWM2M_URI_MQTT;
        port = SIRENA_LWM2M_URI_MQTT_PORT;
        break;
    case SIRENA_LWM2M_URI_SCHEME_MQTTS:
        prefix = SIRENA_LWM2M_URI_MQTTS;
        port = SIRENA_LWM2M_URI_MQTTS_PORT;
        break;
    default:
        return -EINVAL;
    }

    if (!uri_has_scheme(server_uri, prefix)) {
        return -EINVAL;
    }

    host = server_uri + strlen(prefix);

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
