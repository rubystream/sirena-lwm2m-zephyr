#include "sirena_lwm2m_bindings.h"
#include "sirena_lwm2m_coap_endpoint.h"
#include "sirena_lwm2m_exchange.h"
#include <sirena/lwm2m/dtls.h>
#include <sirena/lwm2m/engine.h>
#include <sirena/lwm2m/objects.h>
#include <sirena/lwm2m/protocol.h>
#include <sirena/lwm2m/transport.h>
#include <zephyr/kernel.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/sys/util.h>
#include <mbedtls/ssl_ciphersuites.h>
#include <errno.h>
#include <string.h>

#define SIRENA_LWM2M_SECURITY_OBJECT_ID 0U

#define SIRENA_LWM2M_SECURITY_MODE_RESOURCE 2U
#define SIRENA_LWM2M_IDENTITY_RESOURCE 3U
#define SIRENA_LWM2M_SECRET_KEY_RESOURCE 5U

static bool transport_started;
static bool session_active;
static int client_socket = -1;
static char location_path[CONFIG_SIRENA_LWM2M_LOCATION_PATH_MAX + 1U];
static char request_path[CONFIG_SIRENA_LWM2M_COAP_PATH_MAX + 1U];
static char registration_payload[CONFIG_SIRENA_LWM2M_REGISTRATION_PAYLOAD_MAX + 1U];

static int security_mode_allowed(int64_t mode)
{
    switch (mode) {
    case SIRENA_LWM2M_SECURITY_MODE_PSK:
        return 0;
    case SIRENA_LWM2M_SECURITY_MODE_NOSEC:
        return -EINVAL;
    default:
        return -ENOTSUP;
    }
}

static void credentials_release(void)
{
    (void)tls_credential_delete(CONFIG_SIRENA_LWM2M_DTLS_SEC_TAG, TLS_CREDENTIAL_PSK);
    (void)tls_credential_delete(CONFIG_SIRENA_LWM2M_DTLS_SEC_TAG, TLS_CREDENTIAL_PSK_ID);
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

    if (identity_length == 0U) {
        return -ENOENT;
    }

    if (identity_length > CONFIG_SIRENA_LWM2M_PSK_IDENTITY_MAX) {
        return -EINVAL;
    }

    if (secret_key_length == 0U) {
        return -ENOENT;
    }

    if (secret_key_length > CONFIG_SIRENA_LWM2M_PSK_KEY_MAX) {
        return -EINVAL;
    }

    if (secret_key_length > MBEDTLS_PSK_MAX_LEN) {
        return -ENOMEM;
    }

    credentials_release();

    rc = tls_credential_add(CONFIG_SIRENA_LWM2M_DTLS_SEC_TAG, TLS_CREDENTIAL_PSK, secret_key,
                             secret_key_length);
    if (rc != 0) {
        return rc;
    }

    rc = tls_credential_add(CONFIG_SIRENA_LWM2M_DTLS_SEC_TAG, TLS_CREDENTIAL_PSK_ID, identity,
                             identity_length);
    if (rc != 0) {
        credentials_release();

        return rc;
    }

    return 0;
}

static void dtls_session_close(void)
{
    sirena_lwm2m_coap_endpoint_stop();

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
    sec_tag_t tags[] = { CONFIG_SIRENA_LWM2M_DTLS_SEC_TAG };
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

    rc = security_mode_allowed(mode);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_exchange_server_address(&storage, SIRENA_LWM2M_EXCHANGE_SCHEME_COAPS);
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

    rc = sirena_lwm2m_coap_endpoint_start(client_socket);
    if (rc != 0) {
        dtls_session_close();

        return rc;
    }

    session_active = true;

    return 0;
}

static int dtls_request(const char *path, enum coap_method method, bool content_format_present,
                         uint16_t content_format, const uint8_t *payload, size_t payload_length,
                         char *location, size_t location_capacity)
{
    if (!transport_started) {
        return -ENOTSUP;
    }

    return sirena_lwm2m_exchange_request(SIRENA_LWM2M_EXCHANGE_SCHEME_COAPS, path, method,
                                          content_format_present, content_format, payload,
                                          payload_length, location, location_capacity);
}

static int dtls_register(void)
{
    size_t payload_length = 0U;
    int rc;

    if (!transport_started) {
        return -ENOTSUP;
    }

    rc = sirena_lwm2m_exchange_register(request_path, sizeof(request_path),
                                         registration_payload, sizeof(registration_payload),
                                         &payload_length);
    if (rc != 0) {
        return rc;
    }

    location_path[0] = '\0';

    dtls_session_close();

    rc = dtls_session_open();
    if (rc != 0) {
        return rc;
    }

    rc = dtls_request(request_path, COAP_METHOD_POST, true,
                      COAP_CONTENT_FORMAT_APP_LINK_FORMAT,
                      (const uint8_t *)registration_payload, payload_length, location_path,
                      sizeof(location_path));
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
    int rc;

    if (location_path[0] == '\0' || !session_active) {
        return -EINVAL;
    }

    rc = sirena_lwm2m_exchange_update(location_path, request_path, sizeof(request_path));
    if (rc != 0) {
        return rc;
    }

    return dtls_request(request_path, COAP_METHOD_POST, true,
                        COAP_CONTENT_FORMAT_APP_LINK_FORMAT, NULL, 0U, NULL, 0U);
}

static int dtls_deregister(void)
{
    int rc;

    if (location_path[0] == '\0' || !session_active) {
        return -EINVAL;
    }

    rc = dtls_request(location_path, COAP_METHOD_DELETE, true,
                      COAP_CONTENT_FORMAT_APP_LINK_FORMAT, NULL, 0U, NULL, 0U);
    if (rc == 0) {
        location_path[0] = '\0';
    }

    return rc;
}

static int dtls_poll(int timeout_ms)
{
    return sirena_lwm2m_coap_endpoint_poll(timeout_ms);
}

static int dtls_notify(uint16_t object_id, uint16_t instance_id, uint16_t resource_id)
{
    return sirena_lwm2m_coap_endpoint_notify(object_id, instance_id, resource_id);
}

const struct sirena_lwm2m_transport_ops sirena_lwm2m_dtls_transport_ops = {
    .register_client = dtls_register,
    .update = dtls_update,
    .deregister = dtls_deregister,
    .poll = dtls_poll,
    .notify = dtls_notify,
};

int sirena_lwm2m_dtls_start_engine(void)
{
    struct net_sockaddr_storage storage;
    int64_t mode;
    int rc;

    rc = sirena_lwm2m_get_integer(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                  SIRENA_LWM2M_SECURITY_MODE_RESOURCE, &mode);
    if (rc != 0) {
        return rc;
    }

    rc = security_mode_allowed(mode);
    if (rc != 0) {
        return rc;
    }

    if (transport_started) {
        return 0;
    }

    rc = sirena_lwm2m_protocol_init();
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_exchange_server_address(&storage, SIRENA_LWM2M_EXCHANGE_SCHEME_COAPS);
    if (rc != 0) {
        return rc;
    }

    transport_started = true;

    return 0;
}

int sirena_lwm2m_dtls_stop_engine(void)
{
    dtls_session_close();
    location_path[0] = '\0';
    transport_started = false;

    return 0;
}

int sirena_lwm2m_dtls_start(void)
{
    int rc;

    rc = sirena_lwm2m_transport_engine_ensure();
    if (rc != 0) {
        return rc;
    }

    return sirena_lwm2m_engine_submit(SIRENA_LWM2M_ENGINE_OPERATION_START_DTLS, NULL);
}

int sirena_lwm2m_dtls_stop(void)
{
    if (sirena_lwm2m_engine_thread_id() == NULL) {
        return 0;
    }

    return sirena_lwm2m_engine_submit(SIRENA_LWM2M_ENGINE_OPERATION_STOP_DTLS, NULL);
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
