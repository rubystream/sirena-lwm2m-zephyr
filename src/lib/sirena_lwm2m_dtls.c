#include "sirena_lwm2m_coap_client.h"
#include <sirena/lwm2m/dtls.h>
#include <sirena/lwm2m/object_registry.h>
#include <sirena/lwm2m/objects.h>
#include <sirena/lwm2m/transport.h>
#include <zephyr/kernel.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/coap_client.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/sys/util.h>
#include <mbedtls/ssl_ciphersuites.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIRENA_LWM2M_SECURITY_OBJECT_ID 0U
#define SIRENA_LWM2M_SERVER_OBJECT_ID 1U
#define SIRENA_LWM2M_DEVICE_OBJECT_ID 3U

#define SIRENA_LWM2M_SECURITY_MODE_RESOURCE 2U
#define SIRENA_LWM2M_IDENTITY_RESOURCE 3U
#define SIRENA_LWM2M_SECRET_KEY_RESOURCE 5U

#define SIRENA_LWM2M_COAPS_SCHEME "coaps://"
#define SIRENA_LWM2M_DTLS_DEFAULT_PORT 5684
#define SIRENA_LWM2M_COAP_REGISTER_PATH "rd"
#define SIRENA_LWM2M_COAP_LWM2M_VERSION "1.2"
#define SIRENA_LWM2M_COAP_BINDING_MODE "U"
#define SIRENA_LWM2M_COAP_LOCATION_OPTION 7U

#define SIRENA_LWM2M_DTLS_SEC_TAG 1

struct sirena_lwm2m_dtls_exchange {
    int result_code;
    char location[CONFIG_SIRENA_LWM2M_LOCATION_PATH_MAX + 1U];
};

struct sirena_lwm2m_dtls_payload {
    char *buffer;
    size_t capacity;
    size_t length;
    bool server_present;
    bool device_present;
};

static K_SEM_DEFINE(response_semaphore, 0, 1);

static struct sirena_lwm2m_dtls_exchange exchange;
static struct coap_option location_options[CONFIG_SIRENA_LWM2M_COAP_LOCATION_SEGMENTS];
static bool exchange_pending;
static struct coap_client *client;
static struct coap_client_request client_request;
static bool transport_started;
static bool session_active;
static int client_socket = -1;
static char location_path[CONFIG_SIRENA_LWM2M_LOCATION_PATH_MAX + 1U];
static char request_path[CONFIG_SIRENA_LWM2M_COAP_PATH_MAX + 1U];
static char registration_payload[CONFIG_SIRENA_LWM2M_REGISTRATION_PAYLOAD_MAX + 1U];
static char server_uri[CONFIG_SIRENA_LWM2M_SERVER_URI_MAX + 1U];

static void response_location(struct sirena_lwm2m_dtls_exchange *exchange_data,
                               const struct coap_packet *packet)
{
    size_t length = 0U;
    int count;
    int i;

    exchange_data->location[0] = '\0';

    count = coap_find_options(packet, COAP_OPTION_LOCATION_PATH, location_options,
                              ARRAY_SIZE(location_options));
    if (count > 0) {
        for (i = 0; i < count; ++i) {
            if (length + location_options[i].len + 2U > sizeof(exchange_data->location)) {
                break;
            }

            exchange_data->location[length++] = '/';
            memcpy(&exchange_data->location[length], location_options[i].value,
                   location_options[i].len);
            length += location_options[i].len;
        }

        exchange_data->location[length] = '\0';

        return;
    }

    count = coap_find_options(packet, SIRENA_LWM2M_COAP_LOCATION_OPTION, location_options,
                              ARRAY_SIZE(location_options));
    if (count > 0 && location_options[0].len > 2U &&
        location_options[0].len < sizeof(exchange_data->location)) {
        memcpy(&exchange_data->location[0], location_options[0].value, location_options[0].len);
        exchange_data->location[location_options[0].len] = '\0';
    }
}

static void response_handler(const struct coap_client_response_data *data, void *user_data)
{
    (void)user_data;

    if (!exchange_pending) {
        return;
    }

    exchange.result_code = data->result_code;

    if (data->packet != NULL) {
        response_location(&exchange, data->packet);
    } else {
        exchange.location[0] = '\0';
    }

    k_sem_give(&response_semaphore);
}

static int response_status(int16_t result_code)
{
    if (result_code < 0) {
        return result_code;
    }

    switch (result_code) {
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

static int server_address(struct net_sockaddr_storage *storage)
{
    struct net_sockaddr_in *address = (struct net_sockaddr_in *)storage;
    size_t length = sizeof(server_uri);
    char *host;
    char *separator;
    unsigned long port = SIRENA_LWM2M_DTLS_DEFAULT_PORT;
    int rc;

    rc = sirena_lwm2m_get_string(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U, 0U, server_uri, &length);
    if (rc != 0) {
        return rc;
    }

    if (strncmp(server_uri, SIRENA_LWM2M_COAPS_SCHEME, strlen(SIRENA_LWM2M_COAPS_SCHEME)) != 0) {
        return -EINVAL;
    }

    host = server_uri + strlen(SIRENA_LWM2M_COAPS_SCHEME);
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

static int payload_append(uint16_t object_id, uint16_t instance_id, void *user_data)
{
    struct sirena_lwm2m_dtls_payload *payload = user_data;
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
    return sirena_lwm2m_get_integer(SIRENA_LWM2M_SERVER_OBJECT_ID, 0U, 1U, lifetime);
}

static void credentials_release(void)
{
    (void)tls_credential_delete(SIRENA_LWM2M_DTLS_SEC_TAG, TLS_CREDENTIAL_PSK);
    (void)tls_credential_delete(SIRENA_LWM2M_DTLS_SEC_TAG, TLS_CREDENTIAL_PSK_ID);
}

static int credentials_register(void)
{
    const void *identity = NULL;
    const void *secret_key = NULL;
    size_t identity_length = 0U;
    size_t secret_key_length = 0U;
    int rc;

    rc = sirena_lwm2m_get_opaque_ref(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                      SIRENA_LWM2M_IDENTITY_RESOURCE, &identity,
                                      &identity_length);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_get_opaque_ref(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                      SIRENA_LWM2M_SECRET_KEY_RESOURCE, &secret_key,
                                      &secret_key_length);
    if (rc != 0) {
        return rc;
    }

    if (identity_length == 0U || identity_length > CONFIG_SIRENA_LWM2M_PSK_IDENTITY_MAX) {
        return -ENOENT;
    }

    if (secret_key_length == 0U || secret_key_length > CONFIG_SIRENA_LWM2M_PSK_KEY_MAX) {
        return -ENOENT;
    }

    if (secret_key_length > MBEDTLS_PSK_MAX_LEN) {
        return -ENOMEM;
    }

    credentials_release();

    rc = tls_credential_add(SIRENA_LWM2M_DTLS_SEC_TAG, TLS_CREDENTIAL_PSK, secret_key,
                             secret_key_length);
    if (rc != 0) {
        return rc;
    }

    rc = tls_credential_add(SIRENA_LWM2M_DTLS_SEC_TAG, TLS_CREDENTIAL_PSK_ID, identity,
                             identity_length);
    if (rc != 0) {
        credentials_release();

        return rc;
    }

    return 0;
}

static void dtls_session_close(void)
{
    if (client_socket >= 0) {
        (void)zsock_close(client_socket);
        client_socket = -1;
    }

    credentials_release();
    session_active = false;
}

static int dtls_session_open(void)
{
    static const int ciphersuites[] = { MBEDTLS_TLS_PSK_WITH_AES_128_CCM_8 };
    sec_tag_t tags[] = { SIRENA_LWM2M_DTLS_SEC_TAG };
    struct net_sockaddr_storage storage;
    int verify = ZSOCK_TLS_PEER_VERIFY_NONE;
    int64_t mode;
    int rc;

    if (session_active) {
        return 0;
    }

    rc = sirena_lwm2m_get_integer(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                  SIRENA_LWM2M_SECURITY_MODE_RESOURCE, &mode);
    if (rc != 0) {
        return rc;
    }

    if (mode != SIRENA_LWM2M_SECURITY_MODE_PSK) {
        return -EPROTONOSUPPORT;
    }

    rc = server_address(&storage);
    if (rc != 0) {
        return rc;
    }

    rc = credentials_register();
    if (rc != 0) {
        return rc;
    }

    client_socket = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_DTLS_1_2);
    if (client_socket < 0) {
        rc = -errno;
        client_socket = -1;
        credentials_release();

        return rc;
    }

    rc = zsock_setsockopt(client_socket, ZSOCK_SOL_TLS, ZSOCK_TLS_SEC_TAG_LIST, tags,
                          sizeof(tags));
    if (rc != 0) {
        rc = -errno;
        dtls_session_close();

        return rc;
    }

    rc = zsock_setsockopt(client_socket, ZSOCK_SOL_TLS, ZSOCK_TLS_CIPHERSUITE_LIST,
                          ciphersuites, sizeof(ciphersuites));
    if (rc != 0) {
        rc = -errno;
        dtls_session_close();

        return rc;
    }

    rc = zsock_setsockopt(client_socket, ZSOCK_SOL_TLS, ZSOCK_TLS_PEER_VERIFY, &verify,
                          sizeof(verify));
    if (rc != 0) {
        rc = -errno;
        dtls_session_close();

        return rc;
    }

    rc = zsock_connect(client_socket, net_sad(&storage), sizeof(struct net_sockaddr_in));
    if (rc != 0) {
        dtls_session_close();

        return -EACCES;
    }

    session_active = true;

    return 0;
}

static int dtls_request(const char *path, enum coap_method method, const uint8_t *payload,
                         size_t payload_length, char *location, size_t location_capacity)
{
    struct net_sockaddr_storage storage;
    int rc;

    if (!transport_started) {
        return -ENOTSUP;
    }

    rc = server_address(&storage);
    if (rc != 0) {
        return rc;
    }

    memset(&client_request, 0, sizeof(client_request));
    client_request.method = method;
    client_request.confirmable = true;
    client_request.fmt = COAP_CONTENT_FORMAT_APP_LINK_FORMAT;
    client_request.payload = payload;
    client_request.len = payload_length;
    client_request.cb = response_handler;
    client_request.user_data = &exchange;

    if (strlen(path) >= sizeof(client_request.path)) {
        return -EINVAL;
    }

    strcpy(client_request.path, path);

    exchange.result_code = 0;
    exchange.location[0] = '\0';
    exchange_pending = true;

    k_sem_reset(&response_semaphore);

    rc = coap_client_req(client, client_socket, net_sad(&storage), &client_request, NULL);
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

    rc = response_status(exchange.result_code);
    if (rc != 0) {
        return rc;
    }

    if (location != NULL) {
        size_t length = strlen(exchange.location);

        if (length >= location_capacity) {
            return -ENOMEM;
        }

        memcpy(location, exchange.location, length + 1U);
    }

    return 0;
}

static int dtls_register(void)
{
    struct sirena_lwm2m_dtls_payload context = {
        .buffer = registration_payload,
        .capacity = sizeof(registration_payload),
        .length = 0U,
    };
    const char *endpoint_name = sirena_lwm2m_transport_endpoint_name();
    int64_t lifetime;
    int rc;
    int written;

    if (!transport_started) {
        return -ENOTSUP;
    }

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

    written = snprintf(request_path, sizeof(request_path), "%s?ep=%s&lt=%lld&lwm2m=%s&b=%s",
                       SIRENA_LWM2M_COAP_REGISTER_PATH, endpoint_name,
                       (long long)lifetime, SIRENA_LWM2M_COAP_LWM2M_VERSION,
                       SIRENA_LWM2M_COAP_BINDING_MODE);
    if (written < 0 || (size_t)written >= sizeof(request_path)) {
        return -ENOMEM;
    }

    location_path[0] = '\0';

    dtls_session_close();

    rc = dtls_session_open();
    if (rc != 0) {
        return rc;
    }

    rc = dtls_request(request_path, COAP_METHOD_POST, (const uint8_t *)registration_payload,
                      context.length, location_path, sizeof(location_path));
    if (rc != 0) {
        return rc;
    }

    if (location_path[0] == '\0') {
        return -EPROTO;
    }

    return 0;
}

static int dtls_update(void)
{
    int64_t lifetime;
    int rc;
    int written;

    if (location_path[0] == '\0' || !session_active) {
        return -EINVAL;
    }

    rc = server_lifetime(&lifetime);
    if (rc != 0) {
        return rc;
    }

    written = snprintf(request_path, sizeof(request_path), "%s?lt=%lld&b=%s", location_path,
                       (long long)lifetime, SIRENA_LWM2M_COAP_BINDING_MODE);
    if (written < 0 || (size_t)written >= sizeof(request_path)) {
        return -ENOMEM;
    }

    return dtls_request(request_path, COAP_METHOD_POST, NULL, 0U, NULL, 0U);
}

static int dtls_deregister(void)
{
    int rc;

    if (location_path[0] == '\0' || !session_active) {
        return -EINVAL;
    }

    rc = dtls_request(location_path, COAP_METHOD_DELETE, NULL, 0U, NULL, 0U);
    if (rc == 0) {
        location_path[0] = '\0';
    }

    return rc;
}

static const struct sirena_lwm2m_transport_ops dtls_transport_ops = {
    .register_client = dtls_register,
    .update = dtls_update,
    .deregister = dtls_deregister,
};

int sirena_lwm2m_dtls_start(void)
{
    struct net_sockaddr_storage storage;
    int64_t mode;
    int rc;

    if (transport_started) {
        return 0;
    }

    rc = server_address(&storage);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_get_integer(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                  SIRENA_LWM2M_SECURITY_MODE_RESOURCE, &mode);
    if (rc != 0) {
        return rc;
    }

    if (mode != SIRENA_LWM2M_SECURITY_MODE_PSK) {
        return -EPROTONOSUPPORT;
    }

    rc = sirena_lwm2m_coap_client_get(&client);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_transport_bind(&dtls_transport_ops);
    if (rc != 0) {
        return rc;
    }

    transport_started = true;

    return 0;
}

int sirena_lwm2m_dtls_stop(void)
{
    dtls_session_close();
    location_path[0] = '\0';
    transport_started = false;

    return 0;
}

int sirena_lwm2m_dtls_get_location_path(char *buffer, size_t *length)
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
