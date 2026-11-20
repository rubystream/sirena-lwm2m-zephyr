#include "sirena_lwm2m_bindings.h"
#include "sirena_lwm2m_coap_client.h"
#include "sirena_lwm2m_exchange.h"
#include <sirena/lwm2m/coap.h>
#include <sirena/lwm2m/engine.h>
#include <sirena/lwm2m/objects.h>
#include <sirena/lwm2m/transport.h>
#include <zephyr/kernel.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/coap_client.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <string.h>

#define SIRENA_LWM2M_SECURITY_OBJECT_ID 0U
#define SIRENA_LWM2M_SECURITY_MODE_RESOURCE 2U

static struct coap_client *client;
static bool transport_started;
static int client_socket = -1;
static char location_path[CONFIG_SIRENA_LWM2M_LOCATION_PATH_MAX + 1U];
static char request_path[CONFIG_SIRENA_LWM2M_COAP_PATH_MAX + 1U];
static char registration_payload[CONFIG_SIRENA_LWM2M_REGISTRATION_PAYLOAD_MAX + 1U];

static int security_mode_allowed(int64_t mode)
{
    switch (mode) {
    case SIRENA_LWM2M_SECURITY_MODE_NOSEC:
        if (!IS_ENABLED(CONFIG_SIRENA_LWM2M_TRANSPORT_NOSEC)) {
            return -EPERM;
        }
        return 0;
    case SIRENA_LWM2M_SECURITY_MODE_PSK:
        return -EINVAL;
    default:
        return -ENOTSUP;
    }
}

static int security_mode_check(void)
{
    int64_t mode;
    int rc;

    rc = sirena_lwm2m_get_integer(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                  SIRENA_LWM2M_SECURITY_MODE_RESOURCE, &mode);
    if (rc != 0) {
        return rc;
    }

    return security_mode_allowed(mode);
}

static int coap_request(const char *path, enum coap_method method, const uint8_t *payload,
                         size_t payload_length, char *location, size_t location_capacity)
{
    struct net_sockaddr_storage storage;
    int rc;

    if (!transport_started) {
        return -ENOTSUP;
    }

    rc = security_mode_check();
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_exchange_server_address(&storage, SIRENA_LWM2M_EXCHANGE_SCHEME_COAP);
    if (rc != 0) {
        return rc;
    }

    return sirena_lwm2m_exchange_request(client, client_socket, net_sad(&storage), path, method,
                                         payload, payload_length, location, location_capacity);
}

static int coap_register(void)
{
    size_t payload_length = 0U;
    int rc;

    rc = sirena_lwm2m_exchange_register(request_path, sizeof(request_path),
                                         registration_payload, sizeof(registration_payload),
                                         &payload_length);
    if (rc != 0) {
        return rc;
    }

    rc = coap_request(request_path, COAP_METHOD_POST, (const uint8_t *)registration_payload,
                      payload_length, location_path, sizeof(location_path));
    if (rc != 0) {
        return rc;
    }

    if (location_path[0] == '\0') {
        return -EPROTO;
    }

    return 0;
}

static int coap_update(void)
{
    int rc;

    if (location_path[0] == '\0') {
        return -EINVAL;
    }

    rc = sirena_lwm2m_exchange_update(location_path, request_path, sizeof(request_path));
    if (rc != 0) {
        return rc;
    }

    return coap_request(request_path, COAP_METHOD_POST, NULL, 0U, NULL, 0U);
}

static int coap_deregister(void)
{
    int rc;

    if (location_path[0] == '\0') {
        return -EINVAL;
    }

    rc = coap_request(location_path, COAP_METHOD_DELETE, NULL, 0U, NULL, 0U);
    if (rc == 0) {
        location_path[0] = '\0';
    }

    return rc;
}

const struct sirena_lwm2m_transport_ops sirena_lwm2m_coap_transport_ops = {
    .register_client = coap_register,
    .update = coap_update,
    .deregister = coap_deregister,
};

int sirena_lwm2m_coap_set_endpoint_name(const char *name)
{
    return sirena_lwm2m_transport_set_endpoint_name(name);
}

int sirena_lwm2m_coap_start_engine(void)
{
    struct net_sockaddr_storage storage;
    int rc;

    rc = security_mode_check();
    if (rc != 0) {
        return rc;
    }

    if (transport_started) {
        return 0;
    }

    rc = sirena_lwm2m_exchange_server_address(&storage, SIRENA_LWM2M_EXCHANGE_SCHEME_COAP);
    if (rc != 0) {
        return rc;
    }

    client_socket = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
    if (client_socket < 0) {
        return -errno;
    }

    rc = sirena_lwm2m_coap_client_get(&client);
    if (rc != 0) {
        (void)zsock_close(client_socket);
        client_socket = -1;
        return rc;
    }

    transport_started = true;

    return 0;
}

int sirena_lwm2m_coap_stop_engine(void)
{
    if (client_socket >= 0) {
        (void)zsock_close(client_socket);
        client_socket = -1;
    }

    location_path[0] = '\0';
    transport_started = false;

    return 0;
}

int sirena_lwm2m_coap_start(void)
{
    int rc;

    rc = sirena_lwm2m_transport_engine_ensure();
    if (rc != 0) {
        return rc;
    }

    return sirena_lwm2m_engine_submit(SIRENA_LWM2M_ENGINE_OPERATION_START_COAP);
}

int sirena_lwm2m_coap_stop(void)
{
    if (sirena_lwm2m_engine_thread_id() == NULL) {
        return 0;
    }

    return sirena_lwm2m_engine_submit(SIRENA_LWM2M_ENGINE_OPERATION_STOP_COAP);
}

int sirena_lwm2m_coap_get_location_path(char *buffer, size_t *length)
{
    size_t capacity;
    size_t produced;

    if (buffer == NULL || length == NULL) {
        return -EINVAL;
    }

    capacity = *length;
    if (capacity == 0U) {
        return -ENOMEM;
    }

    produced = strlen(location_path);
    if (produced >= capacity) {
        return -ENOMEM;
    }

    memcpy(buffer, location_path, produced + 1U);
    *length = produced;

    return 0;
}
