#include "sirena_lwm2m_exchange.h"
#include <sirena/lwm2m/object_registry.h>
#include <sirena/lwm2m/objects.h>
#include <sirena/lwm2m/transport.h>
#include <zephyr/kernel.h>
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
#define SIRENA_LWM2M_COAP_LOCATION_OPTION 7U

struct sirena_lwm2m_exchange_payload {
    char *buffer;
    size_t capacity;
    size_t length;
    bool server_present;
    bool device_present;
};

static K_SEM_DEFINE(response_semaphore, 0, 1);

static struct coap_option location_options[CONFIG_SIRENA_LWM2M_COAP_LOCATION_SEGMENTS];
static struct coap_client_request client_request;
static int result_code;
static char response_location[CONFIG_SIRENA_LWM2M_LOCATION_PATH_MAX + 1U];
static bool exchange_pending;
static char server_uri[CONFIG_SIRENA_LWM2M_SERVER_URI_MAX + 1U];

static void location_from_packet(const struct coap_packet *packet)
{
    size_t length = 0U;
    int count;
    int i;

    response_location[0] = '\0';

    count = coap_find_options(packet, COAP_OPTION_LOCATION_PATH, location_options,
                              ARRAY_SIZE(location_options));
    if (count > 0) {
        for (i = 0; i < count; ++i) {
            if (length + location_options[i].len + 2U > sizeof(response_location)) {
                break;
            }

            response_location[length++] = '/';
            memcpy(&response_location[length], location_options[i].value,
                   location_options[i].len);
            length += location_options[i].len;
        }

        response_location[length] = '\0';

        return;
    }

    count = coap_find_options(packet, SIRENA_LWM2M_COAP_LOCATION_OPTION, location_options,
                              ARRAY_SIZE(location_options));
    if (count > 0 && location_options[0].len > 2U &&
        location_options[0].len < sizeof(response_location)) {
        memcpy(response_location, location_options[0].value, location_options[0].len);
        response_location[location_options[0].len] = '\0';
    }
}

static void response_handler(const struct coap_client_response_data *data, void *user_data)
{
    (void)user_data;

    if (!exchange_pending) {
        return;
    }

    result_code = data->result_code;

    if (data->packet != NULL) {
        location_from_packet(data->packet);
    } else {
        response_location[0] = '\0';
    }

    k_sem_give(&response_semaphore);
}

int sirena_lwm2m_exchange_status(int16_t code)
{
    if (code < 0) {
        return code;
    }

    switch (code) {
    case COAP_RESPONSE_CODE_CREATED:
    case COAP_RESPONSE_CODE_DELETED:
    case COAP_RESPONSE_CODE_CHANGED:
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

int sirena_lwm2m_exchange_request(struct coap_client *client, int socket,
                                  const struct net_sockaddr *address, const char *path,
                                  enum coap_method method, const uint8_t *payload,
                                  size_t payload_length, char *location,
                                  size_t location_capacity)
{
    int rc;

    memset(&client_request, 0, sizeof(client_request));
    client_request.method = method;
    client_request.confirmable = true;
    client_request.fmt = COAP_CONTENT_FORMAT_APP_LINK_FORMAT;
    client_request.payload = payload;
    client_request.len = payload_length;
    client_request.cb = response_handler;
    client_request.user_data = NULL;

    if (strlen(path) >= sizeof(client_request.path)) {
        return -EINVAL;
    }

    strcpy(client_request.path, path);

    result_code = 0;
    response_location[0] = '\0';
    exchange_pending = true;

    k_sem_reset(&response_semaphore);

    rc = coap_client_req(client, socket, address, &client_request, NULL);
    if (rc != 0) {
        exchange_pending = false;
        return rc;
    }

    rc = k_sem_take(&response_semaphore,
                    K_MSEC(CONFIG_SIRENA_LWM2M_COAP_RESPONSE_TIMEOUT_MS));
    if (rc != 0) {
        (void)coap_client_cancel_requests(client);
        exchange_pending = false;
        return -ETIMEDOUT;
    }

    exchange_pending = false;

    rc = sirena_lwm2m_exchange_status(result_code);
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
