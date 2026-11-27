#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/sys/util.h>
#include "sirena_lwm2m_mqtt_payload.h"
#include <sirena/lwm2m/codec.h>
#include <sirena/lwm2m/mqtt.h>
#include <sirena/lwm2m/object_registry.h>
#include <sirena/lwm2m/objects.h>
#include <sirena/lwm2m/protocol.h>
#include <sirena/lwm2m/transport.h>
#include <mbedtls/ssl_ciphersuites.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define PEER_ENDPOINT_NAME "sirena-mqtt-test"
#define PEER_CLIENT_ID "sirena-mqtt-client"
#define PEER_LIFETIME 300U
#define PEER_LIFETIME_UPDATED 600U
#define PEER_LIFETIME_NOTIFIED 900U

#define PEER_SERVER_OBJECT_ID 1U
#define PEER_DEVICE_OBJECT_ID 3U
#define PEER_INSTANCE_ID 0U
#define PEER_SERVER_LIFETIME_RESOURCE 1U
#define PEER_DEVICE_REBOOT_RESOURCE 4U

#define PEER_OBJECT_LIST "</1/0>,</3/0>"
#define PEER_TOPIC_MAX 128
#define PEER_PAYLOAD_MAX 512
#define PEER_PUBLISH_MAX 16
#define PEER_WAIT_MS 5000
#define PEER_POLL_MS 10

#define PEER_PACKET_CONNECT 0x10U
#define PEER_PACKET_CONNACK 0x20U
#define PEER_PACKET_PUBLISH 0x30U
#define PEER_PACKET_SUBSCRIBE 0x80U
#define PEER_PACKET_SUBACK 0x90U
#define PEER_PACKET_PINGREQ 0xC0U
#define PEER_PACKET_PINGRESP 0xD0U
#define PEER_PACKET_DISCONNECT 0xE0U

#define PEER_TLS_SEC_TAG 3
#define PEER_PSK_IDENTITY "sirena-mqtt-psk"
#define PEER_PSK_KEY "0123456789abcdef"
#define PEER_LINK_FORMAT_CONTENT_FORMAT 40U
#define PEER_OPERATION_READ_COMPOSITE 10U
#define PEER_CBOR_MAX_DEPTH 8U

struct peer_publish {
    char topic[PEER_TOPIC_MAX];
    uint8_t payload[PEER_PAYLOAD_MAX];
    size_t payload_length;
};

struct peer_state {
    bool connected;
    bool client_id_received;
    char client_id[64];
    bool subscribed;
    char subscribe_topic[PEER_TOPIC_MAX];
    uint8_t publish_count;
    uint8_t result_count;
    struct peer_publish publishes[PEER_PUBLISH_MAX];
    uint32_t register_result;
    uint32_t result_token_delta;
};

static struct peer_state peer;
static struct k_mutex peer_lock;
static int peer_listen_socket = -1;
static int peer_client_socket = -1;
static uint16_t peer_port;
static char server_uri[64];
static bool peer_transport_running;
static bool peer_execute_called;
static uint16_t peer_execute_object_id;
static uint16_t peer_execute_resource_id;

static int peer_tls_listen_socket = -1;
static uint16_t peer_tls_port;
static char peer_tls_client_id[64];
static char peer_tls_subscribe_topic[PEER_TOPIC_MAX];
static bool peer_tls_connected;
static bool peer_tls_subscribed;

K_THREAD_STACK_DEFINE(peer_stack, 4096);
static struct k_thread peer_thread;
K_THREAD_STACK_DEFINE(peer_tls_stack, 6144);
static struct k_thread peer_tls_thread;

static int peer_read_exact(int socket, void *buffer, size_t length)
{
    uint8_t *cursor = buffer;
    size_t received = 0U;

    while (received < length) {
        int rc = zsock_recv(socket, cursor + received, length - received, 0);

        if (rc < 0) {
            return rc;
        }

        if (rc == 0) {
            return -ENOTCONN;
        }

        received += (size_t)rc;
    }

    return (int)received;
}

static int peer_read_remaining_length(int socket, uint32_t *remaining)
{
    uint32_t value = 0U;
    uint32_t multiplier = 1U;
    unsigned int i;

    for (i = 0U; i < 4U; ++i) {
        uint8_t byte;
        int rc = peer_read_exact(socket, &byte, 1U);

        if (rc != 1) {
            return rc;
        }

        value += (uint32_t)(byte & 0x7FU) * multiplier;
        multiplier *= 128U;

        if ((byte & 0x80U) == 0U) {
            *remaining = value;
            return 0;
        }
    }

    return -EINVAL;
}

static int peer_write_all(int socket, const void *data, size_t length)
{
    const uint8_t *cursor = data;
    size_t sent = 0U;

    while (sent < length) {
        int rc = zsock_send(socket, cursor + sent, length - sent, 0);

        if (rc < 0) {
            return rc;
        }

        sent += (size_t)rc;
    }

    return 0;
}

static int peer_send_publish_locked(const char *topic, const uint8_t *payload, size_t length)
{
    uint8_t buffer[PEER_PAYLOAD_MAX + PEER_TOPIC_MAX + 8U];
    size_t topic_length = strlen(topic);
    size_t remaining = topic_length + 2U + length;
    size_t offset = 0U;
    uint8_t encoded;

    if (peer_client_socket < 0) {
        return -ENOTCONN;
    }

    buffer[offset++] = PEER_PACKET_PUBLISH;

    do {
        encoded = (uint8_t)(remaining % 128U);
        remaining /= 128U;
        if (remaining > 0U) {
            encoded |= 0x80U;
        }
        buffer[offset++] = encoded;
    } while (remaining > 0U);

    buffer[offset++] = (uint8_t)(topic_length >> 8U);
    buffer[offset++] = (uint8_t)topic_length;
    memcpy(buffer + offset, topic, topic_length);
    offset += topic_length;

    if (length > 0U) {
        memcpy(buffer + offset, payload, length);
        offset += length;
    }

    return peer_write_all(peer_client_socket, buffer, offset);
}

static int peer_send_message_locked(const struct sirena_lwm2m_mqtt_message *message)
{
    uint8_t payload[PEER_PAYLOAD_MAX];
    size_t length = sizeof(payload);
    int rc;

    rc = sirena_lwm2m_mqtt_message_encode(message, payload, &length);
    if (rc != 0) {
        return rc;
    }

    return peer_send_publish_locked(peer.subscribe_topic, payload, length);
}

static int peer_send_result(uint32_t token, uint32_t result)
{
    struct sirena_lwm2m_mqtt_message message = {
        .has_operation = false,
        .token = token + peer.result_token_delta,
        .has_result = true,
        .result = result,
    };

    return peer_send_message_locked(&message);
}

static void peer_record_publish(const char *topic, const uint8_t *payload, size_t length)
{
    struct sirena_lwm2m_mqtt_message message;
    struct peer_publish *publish;

    if (peer.publish_count >= PEER_PUBLISH_MAX) {
        return;
    }

    publish = &peer.publishes[peer.publish_count];
    peer.publish_count++;

    snprintf(publish->topic, sizeof(publish->topic), "%s", topic);
    if (length > sizeof(publish->payload)) {
        length = sizeof(publish->payload);
    }
    memcpy(publish->payload, payload, length);
    publish->payload_length = length;

    if (sirena_lwm2m_mqtt_message_decode(publish->payload, publish->payload_length, &message) != 0) {
        return;
    }

    if (message.has_result) {
        peer.result_count++;
        return;
    }

    if (!message.has_operation) {
        return;
    }

    switch (message.operation) {
    case SIRENA_LWM2M_MQTT_OPERATION_REGISTER:
        (void)peer_send_result(message.token, peer.register_result);
        break;
    case SIRENA_LWM2M_MQTT_OPERATION_UPDATE:
        (void)peer_send_result(message.token, SIRENA_LWM2M_MQTT_RESULT_CHANGED);
        break;
    case SIRENA_LWM2M_MQTT_OPERATION_DEREGISTER:
        (void)peer_send_result(message.token, SIRENA_LWM2M_MQTT_RESULT_DEREGISTERED);
        break;
    default:
        break;
    }
}

static void peer_handle_connect(const uint8_t *body, size_t length)
{
    size_t offset;
    size_t name_length;
    size_t client_id_length;

    if (length < 12U) {
        return;
    }

    name_length = ((size_t)body[0] << 8U) | body[1];
    offset = 2U + name_length + 1U + 1U + 2U;

    if (offset + 2U > length) {
        return;
    }

    client_id_length = ((size_t)body[offset] << 8U) | body[offset + 1U];
    offset += 2U;

    if (offset + client_id_length > length) {
        return;
    }

    if (client_id_length >= sizeof(peer.client_id)) {
        client_id_length = sizeof(peer.client_id) - 1U;
    }

    memcpy(peer.client_id, body + offset, client_id_length);
    peer.client_id[client_id_length] = '\0';
    peer.client_id_received = true;
    peer.connected = true;

    {
        static const uint8_t connack[4] = { PEER_PACKET_CONNACK, 0x02U, 0x00U, 0x00U };

        (void)peer_write_all(peer_client_socket, connack, sizeof(connack));
    }
}

static void peer_handle_subscribe(const uint8_t *body, size_t length)
{
    uint8_t suback[2U + 1U + 16U];
    size_t offset = 2U;
    size_t count = 0U;
    size_t topic_length;

    if (length < 2U) {
        return;
    }

    suback[0] = PEER_PACKET_SUBACK;
    suback[1] = 0U;
    suback[2] = body[0];
    suback[3] = body[1];

    while (offset + 2U <= length) {
        topic_length = ((size_t)body[offset] << 8U) | body[offset + 1U];
        offset += 2U;

        if (offset + topic_length + 1U > length) {
            break;
        }

        if (count == 0U) {
            size_t stored = topic_length;

            if (stored >= sizeof(peer.subscribe_topic)) {
                stored = sizeof(peer.subscribe_topic) - 1U;
            }

            memcpy(peer.subscribe_topic, body + offset, stored);
            peer.subscribe_topic[stored] = '\0';
            peer.subscribed = true;
        }

        offset += topic_length + 1U;
        count++;
    }

    if (count == 0U || count > 16U) {
        return;
    }

    for (size_t i = 0U; i < count; ++i) {
        suback[4U + i] = 0x00U;
    }

    suback[1] = (uint8_t)(2U + count);

    (void)peer_write_all(peer_client_socket, suback, 4U + count);
}

static void peer_handle_publish(const uint8_t *body, size_t length, uint8_t flags)
{
    char topic[PEER_TOPIC_MAX];
    size_t topic_length;
    size_t offset;

    if (length < 2U) {
        return;
    }

    topic_length = ((size_t)body[0] << 8U) | body[1];
    offset = 2U;

    if (offset + topic_length > length) {
        return;
    }

    if (topic_length >= sizeof(topic)) {
        topic_length = sizeof(topic) - 1U;
    }

    memcpy(topic, body + offset, topic_length);
    topic[topic_length] = '\0';
    offset += topic_length;

    if ((flags & 0x06U) != 0U) {
        offset += 2U;
    }

    if (offset > length) {
        return;
    }

    peer_record_publish(topic, body + offset, length - offset);

    if (peer.subscribed && strcmp(topic, peer.subscribe_topic) == 0) {
        (void)peer_send_publish_locked(topic, body + offset, length - offset);
    }
}

static void peer_handle_packet(uint8_t header, const uint8_t *body, size_t length)
{
    uint8_t type = (uint8_t)(header & 0xF0U);
    uint8_t flags = (uint8_t)(header & 0x0FU);

    switch (type) {
    case PEER_PACKET_CONNECT:
        peer_handle_connect(body, length);
        break;
    case PEER_PACKET_SUBSCRIBE:
        peer_handle_subscribe(body, length);
        break;
    case PEER_PACKET_PUBLISH:
        peer_handle_publish(body, length, flags);
        break;
    case PEER_PACKET_PINGREQ: {
        uint8_t response[2] = { PEER_PACKET_PINGRESP, 0x00U };

        (void)peer_write_all(peer_client_socket, response, sizeof(response));
        break;
    }
    case PEER_PACKET_DISCONNECT:
        peer.connected = false;
        break;
    default:
        break;
    }
}

static void peer_serve(int socket)
{
    uint8_t body[PEER_PAYLOAD_MAX + PEER_TOPIC_MAX + 8U];

    k_mutex_lock(&peer_lock, K_FOREVER);
    peer_client_socket = socket;
    k_mutex_unlock(&peer_lock);

    while (true) {
        uint8_t header;
        uint32_t remaining = 0U;
        int rc;

        rc = peer_read_exact(socket, &header, 1U);
        if (rc != 1) {
            break;
        }

        if (peer_read_remaining_length(socket, &remaining) != 0) {
            break;
        }

        if (remaining > sizeof(body)) {
            break;
        }

        if (remaining > 0U) {
            rc = peer_read_exact(socket, body, remaining);
            if ((int)remaining != rc) {
                break;
            }
        }

        k_mutex_lock(&peer_lock, K_FOREVER);
        peer_handle_packet(header, body, remaining);
        k_mutex_unlock(&peer_lock);
    }

    k_mutex_lock(&peer_lock, K_FOREVER);
    peer_client_socket = -1;
    peer.connected = false;
    k_mutex_unlock(&peer_lock);
}

static void peer_thread_entry(void *first, void *second, void *third)
{
    (void)first;
    (void)second;
    (void)third;

    while (true) {
        struct net_sockaddr_in address;
        net_socklen_t length = sizeof(address);
        int socket;

        socket = zsock_accept(peer_listen_socket, (struct net_sockaddr *)&address, &length);
        if (socket < 0) {
            k_msleep(PEER_POLL_MS);
            continue;
        }

        peer_serve(socket);
        (void)zsock_close(socket);
    }
}

static void peer_tls_record_connect(const uint8_t *body, size_t length)
{
    size_t offset;
    size_t name_length;
    size_t client_id_length;

    if (length < 12U) {
        return;
    }

    name_length = ((size_t)body[0] << 8U) | body[1];
    offset = 2U + name_length + 1U + 1U + 2U;

    if (offset + 2U > length) {
        return;
    }

    client_id_length = ((size_t)body[offset] << 8U) | body[offset + 1U];
    offset += 2U;

    if (offset + client_id_length > length ||
        client_id_length >= sizeof(peer_tls_client_id)) {
        return;
    }

    memcpy(peer_tls_client_id, body + offset, client_id_length);
    peer_tls_client_id[client_id_length] = '\0';
}

static void peer_tls_serve(int socket)
{
    uint8_t body[PEER_PAYLOAD_MAX + PEER_TOPIC_MAX + 8U];

    while (true) {
        uint8_t header;
        uint32_t remaining = 0U;
        int rc;

        rc = peer_read_exact(socket, &header, 1U);
        if (rc != 1) {
            break;
        }

        if (peer_read_remaining_length(socket, &remaining) != 0) {
            break;
        }

        if (remaining > sizeof(body)) {
            break;
        }

        if (remaining > 0U) {
            rc = peer_read_exact(socket, body, remaining);
            if ((int)remaining != rc) {
                break;
            }
        }

        switch (header & 0xF0U) {
        case PEER_PACKET_CONNECT: {
            static const uint8_t connack[4] = { PEER_PACKET_CONNACK, 0x02U, 0x00U, 0x00U };

            peer_tls_record_connect(body, remaining);
            peer_tls_connected = true;
            (void)peer_write_all(socket, connack, sizeof(connack));
            break;
        }
        case PEER_PACKET_SUBSCRIBE: {
            uint8_t suback[4] = { PEER_PACKET_SUBACK, 0x03U, 0x00U, 0x00U };
            size_t topic_length;

            if (remaining < 5U) {
                break;
            }

            topic_length = ((size_t)body[2] << 8U) | body[3];
            if (5U + topic_length > remaining ||
                topic_length >= sizeof(peer_tls_subscribe_topic)) {
                break;
            }

            memcpy(peer_tls_subscribe_topic, body + 4U, topic_length);
            peer_tls_subscribe_topic[topic_length] = '\0';
            peer_tls_subscribed = true;

            suback[2] = body[0];
            suback[3] = body[1];
            (void)peer_write_all(socket, suback, sizeof(suback));
            break;
        }
        case PEER_PACKET_PINGREQ: {
            uint8_t response[2] = { PEER_PACKET_PINGRESP, 0x00U };

            (void)peer_write_all(socket, response, sizeof(response));
            break;
        }
        default:
            break;
        }
    }
}

static void peer_tls_thread_entry(void *first, void *second, void *third)
{
    (void)first;
    (void)second;
    (void)third;

    while (true) {
        struct net_sockaddr_in address;
        net_socklen_t length = sizeof(address);
        int socket;

        socket = zsock_accept(peer_tls_listen_socket, (struct net_sockaddr *)&address, &length);
        if (socket < 0) {
            k_msleep(PEER_POLL_MS);
            continue;
        }

        peer_tls_serve(socket);
        (void)zsock_close(socket);
    }
}

static void peer_tls_start(void)
{
    static const int ciphersuites[] = { MBEDTLS_TLS_PSK_WITH_AES_128_CCM_8 };
    sec_tag_t tags[] = { PEER_TLS_SEC_TAG };
    struct net_sockaddr_in address;
    struct net_sockaddr_in bound;
    net_socklen_t length = sizeof(bound);
    int verify = ZSOCK_TLS_PEER_VERIFY_NONE;
    int rc;

    rc = tls_credential_add(PEER_TLS_SEC_TAG, TLS_CREDENTIAL_PSK, PEER_PSK_KEY,
                             sizeof(PEER_PSK_KEY) - 1U);
    zassert_equal(rc, 0, "the peer key registration failed: %d", rc);

    rc = tls_credential_add(PEER_TLS_SEC_TAG, TLS_CREDENTIAL_PSK_ID, PEER_PSK_IDENTITY,
                             sizeof(PEER_PSK_IDENTITY) - 1U);
    zassert_equal(rc, 0, "the peer identity registration failed: %d", rc);

    peer_tls_listen_socket = zsock_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TLS_1_2);
    zassert_true(peer_tls_listen_socket >= 0, "the peer socket creation failed");

    rc = zsock_setsockopt(peer_tls_listen_socket, ZSOCK_SOL_TLS, ZSOCK_TLS_SEC_TAG_LIST, tags,
                          sizeof(tags));
    zassert_equal(rc, 0, "the peer credential setup failed: %d", rc);

    rc = zsock_setsockopt(peer_tls_listen_socket, ZSOCK_SOL_TLS, ZSOCK_TLS_CIPHERSUITE_LIST,
                          ciphersuites, sizeof(ciphersuites));
    zassert_equal(rc, 0, "the peer ciphersuite setup failed: %d", rc);

    rc = zsock_setsockopt(peer_tls_listen_socket, ZSOCK_SOL_TLS, ZSOCK_TLS_PEER_VERIFY, &verify,
                          sizeof(verify));
    zassert_equal(rc, 0, "the peer verification setup failed: %d", rc);

    memset(&address, 0, sizeof(address));
    address.sin_family = NET_AF_INET;
    address.sin_port = net_htons(0U);
    address.sin_addr.s_addr = net_htonl(0x7F000001U);

    rc = zsock_bind(peer_tls_listen_socket, (struct net_sockaddr *)&address, sizeof(address));
    zassert_equal(rc, 0, "the peer bind failed: %d", rc);

    rc = zsock_listen(peer_tls_listen_socket, 8);
    zassert_equal(rc, 0, "the peer listen failed: %d", rc);

    length = sizeof(bound);
    rc = zsock_getsockname(peer_tls_listen_socket, (struct net_sockaddr *)&bound, &length);
    zassert_equal(rc, 0, "the peer port lookup failed: %d", rc);
    peer_tls_port = net_ntohs(bound.sin_port);
    zassert_true(peer_tls_port != 0U, "the peer port is empty");

    k_thread_create(&peer_tls_thread, peer_tls_stack, K_THREAD_STACK_SIZEOF(peer_tls_stack),
                    peer_tls_thread_entry, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
}

static bool peer_decode_publish(const struct peer_publish *publish,
                                 struct sirena_lwm2m_mqtt_message *message)
{
    return sirena_lwm2m_mqtt_message_decode(publish->payload, publish->payload_length,
                                             message) == 0;
}

static bool peer_wait_publish(bool (*match)(const struct sirena_lwm2m_mqtt_message *),
                               size_t index, struct peer_publish *found,
                               struct sirena_lwm2m_mqtt_message *message)
{
    int64_t deadline = k_uptime_get() + PEER_WAIT_MS;

    while (k_uptime_get() < deadline) {
        uint8_t i;
        size_t seen = 0U;

        k_mutex_lock(&peer_lock, K_FOREVER);
        for (i = 0U; i < peer.publish_count; ++i) {
            struct sirena_lwm2m_mqtt_message decoded;

            if (!peer_decode_publish(&peer.publishes[i], &decoded)) {
                continue;
            }

            if (!match(&decoded)) {
                continue;
            }

            if (seen == index) {
                if (found != NULL) {
                    *found = peer.publishes[i];
                }

                if (message != NULL) {
                    *message = decoded;
                }

                k_mutex_unlock(&peer_lock);
                return true;
            }

            seen++;
        }
        k_mutex_unlock(&peer_lock);

        k_msleep(PEER_POLL_MS);
    }

    return false;
}

static bool peer_match_operation(const struct sirena_lwm2m_mqtt_message *message)
{
    return message->has_operation &&
           message->operation == SIRENA_LWM2M_MQTT_OPERATION_REGISTER;
}

static bool peer_match_update(const struct sirena_lwm2m_mqtt_message *message)
{
    return message->has_operation &&
           message->operation == SIRENA_LWM2M_MQTT_OPERATION_UPDATE;
}

static bool peer_match_deregister(const struct sirena_lwm2m_mqtt_message *message)
{
    return message->has_operation &&
           message->operation == SIRENA_LWM2M_MQTT_OPERATION_DEREGISTER;
}

static bool peer_match_result(const struct sirena_lwm2m_mqtt_message *message)
{
    return message->has_result;
}

static bool peer_match_notify(const struct sirena_lwm2m_mqtt_message *message)
{
    return message->has_operation &&
           message->operation == SIRENA_LWM2M_MQTT_OPERATION_NOTIFY;
}

static int peer_start_transport(void)
{
    int rc = sirena_lwm2m_mqtt_start();

    peer_transport_running = rc == 0;

    return rc;
}

static int peer_stop_transport(void)
{
    int rc = sirena_lwm2m_mqtt_stop();

    peer_transport_running = false;

    return rc;
}

static void peer_execute_handler(uint16_t object_id, uint16_t instance_id,
                                 uint16_t resource_id, void *user_data)
{
    (void)instance_id;
    (void)user_data;

    peer_execute_called = true;
    peer_execute_object_id = object_id;
    peer_execute_resource_id = resource_id;
}

static size_t peer_encode_integer(uint16_t object_id, uint16_t resource_id, int64_t value,
                                   uint8_t *buffer, size_t capacity)
{
    struct sirena_lwm2m_codec_value codec_value = {
        .object_id = object_id,
        .instance_id = PEER_INSTANCE_ID,
        .resource_id = resource_id,
        .id = resource_id,
        .type = SIRENA_LWM2M_VALUE_INTEGER,
    };
    size_t length = capacity;
    int rc;

    codec_value.data.integer = value;

    rc = sirena_lwm2m_codec_encode(SIRENA_LWM2M_CF_TLV, &codec_value, 1U, buffer, &length);
    zassert_equal(rc, 0, "the tlv encoding failed: %d", rc);

    return length;
}

static int64_t peer_decode_integer(const struct sirena_lwm2m_mqtt_message *message)
{
    struct sirena_lwm2m_codec_value values[4];
    size_t count = ARRAY_SIZE(values);
    int rc;

    rc = sirena_lwm2m_codec_decode(SIRENA_LWM2M_CF_TLV, SIRENA_LWM2M_VALUE_INTEGER,
                                   message->payload, message->payload_length,
                                   values, &count);
    zassert_equal(rc, 0, "the cbor decoding failed: %d", rc);
    zassert_equal(count, 1U, "unexpected value count");

    return values[0].data.integer;
}

static void peer_wait_subscribed(void)
{
    int64_t deadline = k_uptime_get() + PEER_WAIT_MS;

    while (k_uptime_get() < deadline) {
        bool subscribed;

        k_mutex_lock(&peer_lock, K_FOREVER);
        subscribed = peer.subscribed;
        k_mutex_unlock(&peer_lock);

        if (subscribed) {
            return;
        }

        k_msleep(PEER_POLL_MS);
    }

    zassert_unreachable("the client did not subscribe");
}

static int peer_publish_command(const struct sirena_lwm2m_mqtt_message *message)
{
    uint8_t payload[PEER_PAYLOAD_MAX];
    size_t length = sizeof(payload);
    int rc;

    peer_wait_subscribed();

    rc = sirena_lwm2m_mqtt_message_encode(message, payload, &length);
    zassert_equal(rc, 0, "the command encoding failed: %d", rc);

    k_mutex_lock(&peer_lock, K_FOREVER);
    rc = peer_send_publish_locked(peer.subscribe_topic, payload, length);
    k_mutex_unlock(&peer_lock);

    return rc;
}

static void peer_reset_messages(void)
{
    k_mutex_lock(&peer_lock, K_FOREVER);
    peer.publish_count = 0U;
    peer.result_count = 0U;
    peer.register_result = SIRENA_LWM2M_MQTT_RESULT_REGISTERED;
    peer.result_token_delta = 0U;
    k_mutex_unlock(&peer_lock);
}

static void *suite_setup(void)
{
    struct net_sockaddr_in address;
    net_socklen_t length = sizeof(address);
    int rc;

    rc = k_mutex_init(&peer_lock);
    zassert_equal(rc, 0, "the peer lock could not be initialized");

    peer_listen_socket = zsock_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TCP);
    zassert_true(peer_listen_socket >= 0, "the peer socket creation failed");

    memset(&address, 0, sizeof(address));
    address.sin_family = NET_AF_INET;
    address.sin_port = net_htons(0U);
    address.sin_addr.s_addr = net_htonl(0x7F000001U);

    rc = zsock_bind(peer_listen_socket, (struct net_sockaddr *)&address, sizeof(address));
    zassert_equal(rc, 0, "the peer socket bind failed: %d", rc);

    rc = zsock_listen(peer_listen_socket, 8);
    zassert_equal(rc, 0, "the peer socket listen failed: %d", rc);

    memset(&address, 0, sizeof(address));
    length = sizeof(address);
    rc = zsock_getsockname(peer_listen_socket, (struct net_sockaddr *)&address, &length);
    zassert_equal(rc, 0, "the peer port lookup failed: %d", rc);

    peer_port = net_ntohs(address.sin_port);

    rc = snprintf(server_uri, sizeof(server_uri), "mqtt://127.0.0.1:%u", peer_port);
    zassert_true(rc > 0 && rc < (int)sizeof(server_uri), "the server uri build failed");

    k_thread_create(&peer_thread, peer_stack, K_THREAD_STACK_SIZEOF(peer_stack),
                    peer_thread_entry, NULL, NULL, NULL, 5, 0, K_NO_WAIT);

    peer_tls_start();

    return NULL;
}

static void test_before(void *fixture)
{
    struct sirena_lwm2m_provisioning provisioning = {
        .server_uri = server_uri,
        .security_mode = SIRENA_LWM2M_SECURITY_MODE_NOSEC,
        .short_server_id = 1U,
        .lifetime = PEER_LIFETIME,
        .binding = "M",
    };
    int rc;

    (void)fixture;

    peer_reset_messages();

    peer_execute_called = false;
    peer_execute_object_id = 0U;
    peer_execute_resource_id = 0U;

    rc = sirena_lwm2m_registry_init();
    zassert_equal(rc, 0, "the registry init failed");

    rc = sirena_lwm2m_objects_init();
    zassert_equal(rc, 0, "the objects init failed");

    rc = sirena_lwm2m_set_execute_handler(peer_execute_handler, NULL);
    zassert_equal(rc, 0, "the execute handler registration failed");

    rc = sirena_lwm2m_provision(&provisioning);
    zassert_equal(rc, 0, "the provisioning failed: %d", rc);

    rc = sirena_lwm2m_transport_set_endpoint_name(PEER_ENDPOINT_NAME);
    zassert_equal(rc, 0, "the endpoint name was rejected");

    if (!peer_transport_running) {
        rc = sirena_lwm2m_mqtt_set_client_id("");
        zassert_equal(rc, 0, "the client id reset failed");

        rc = sirena_lwm2m_mqtt_set_topic_prefix("");
        zassert_equal(rc, 0, "the topic prefix reset failed");

        rc = peer_start_transport();
        zassert_equal(rc, 0, "the transport start failed: %d", rc);
    }
}

ZTEST_SUITE(sirena_lwm2m_mqtt, NULL, suite_setup, test_before, NULL, NULL);

static void peer_wait_client_id(const char *client_id)
{
    int64_t deadline = k_uptime_get() + PEER_WAIT_MS;

    while (k_uptime_get() < deadline) {
        bool received;

        k_mutex_lock(&peer_lock, K_FOREVER);
        received = peer.client_id_received && strcmp(peer.client_id, client_id) == 0;
        k_mutex_unlock(&peer_lock);

        if (received) {
            return;
        }

        k_msleep(PEER_POLL_MS);
    }

    zassert_unreachable("the broker received no connect message with the expected client id");
}

ZTEST(sirena_lwm2m_mqtt, test_the_client_connects_with_the_endpoint_name)
{
    peer_wait_client_id(PEER_ENDPOINT_NAME);
    peer_wait_subscribed();

    k_mutex_lock(&peer_lock, K_FOREVER);
    zassert_str_equal(peer.client_id, PEER_ENDPOINT_NAME, "unexpected client id");
    zassert_str_equal(peer.subscribe_topic, "lwm2m/rd/" PEER_ENDPOINT_NAME,
                      "unexpected subscription topic");
    k_mutex_unlock(&peer_lock);
}

ZTEST(sirena_lwm2m_mqtt, test_the_client_identifier_can_be_set)
{
    int rc;

    rc = peer_stop_transport();
    zassert_equal(rc, 0, "the transport stop failed: %d", rc);

    rc = sirena_lwm2m_mqtt_set_client_id(PEER_CLIENT_ID);
    zassert_equal(rc, 0, "the client id was rejected");

    rc = peer_start_transport();
    zassert_equal(rc, 0, "the transport start failed: %d", rc);

    peer_wait_client_id(PEER_CLIENT_ID);

    k_mutex_lock(&peer_lock, K_FOREVER);
    zassert_str_equal(peer.client_id, PEER_CLIENT_ID, "unexpected client id");
    k_mutex_unlock(&peer_lock);

    rc = peer_stop_transport();
    zassert_equal(rc, 0, "the transport stop failed: %d", rc);

    rc = sirena_lwm2m_mqtt_set_client_id("");
    zassert_equal(rc, 0, "the client id reset failed");

    rc = peer_start_transport();
    zassert_equal(rc, 0, "the transport start failed: %d", rc);
}

ZTEST(sirena_lwm2m_mqtt, test_the_topic_prefix_is_part_of_the_topic)
{
    struct sirena_lwm2m_mqtt_message message;
    struct peer_publish publish;
    int rc;

    rc = peer_stop_transport();
    zassert_equal(rc, 0, "the transport stop failed: %d", rc);

    rc = sirena_lwm2m_mqtt_set_topic_prefix("tenant-a");
    zassert_equal(rc, 0, "the topic prefix was rejected");

    rc = peer_start_transport();
    zassert_equal(rc, 0, "the transport start failed: %d", rc);

    peer_wait_subscribed();

    rc = sirena_lwm2m_transport_register();
    zassert_equal(rc, 0, "the registration failed: %d", rc);

    zassert_true(peer_wait_publish(peer_match_operation, 0U, &publish, &message),
                 "the broker received no registration");
    zassert_str_equal(publish.topic, "tenant-a/lwm2m/rd/" PEER_ENDPOINT_NAME,
                      "unexpected registration topic");

    rc = peer_stop_transport();
    zassert_equal(rc, 0, "the transport stop failed: %d", rc);

    rc = sirena_lwm2m_mqtt_set_topic_prefix("");
    zassert_equal(rc, 0, "the topic prefix reset failed");

    rc = peer_start_transport();
    zassert_equal(rc, 0, "the transport start failed: %d", rc);
}

ZTEST(sirena_lwm2m_mqtt, test_the_registration_carries_the_registration_parameters)
{
    struct sirena_lwm2m_mqtt_message message;
    struct peer_publish publish;
    int rc;

    rc = sirena_lwm2m_transport_register();
    zassert_equal(rc, 0, "the registration failed: %d", rc);

    zassert_true(peer_wait_publish(peer_match_operation, 0U, &publish, &message),
                 "the broker received no registration");
    zassert_str_equal(publish.topic, "lwm2m/rd/" PEER_ENDPOINT_NAME,
                      "unexpected registration topic");
    zassert_true(message.has_operation, "the registration carries no operation");
    zassert_equal(message.operation, SIRENA_LWM2M_MQTT_OPERATION_REGISTER,
                  "unexpected registration operation");
    zassert_true(message.has_endpoint, "the registration carries no endpoint name");
    zassert_str_equal(message.endpoint, PEER_ENDPOINT_NAME, "unexpected endpoint name");
    zassert_true(message.has_lifetime, "the registration carries no lifetime");
    zassert_equal(message.lifetime, PEER_LIFETIME, "unexpected lifetime");
    zassert_true(message.has_version, "the registration carries no version");
    zassert_str_equal(message.version, CONFIG_SIRENA_LWM2M_LWM2M_VERSION,
                      "unexpected version");
    zassert_true(message.has_binding, "the registration carries no binding");
    zassert_str_equal(message.binding, "M", "unexpected binding");
    zassert_true(message.has_content_format, "the registration carries no content format");
    zassert_equal(message.content_format, PEER_LINK_FORMAT_CONTENT_FORMAT,
                  "unexpected content format");
    zassert_true(message.has_payload, "the registration carries no object list");
    zassert_true(message.payload_length > 0U, "the object list is empty");
    zassert_equal(message.payload_length, sizeof(PEER_OBJECT_LIST) - 1U,
                  "unexpected object list length");
    zassert_equal(memcmp(message.payload, PEER_OBJECT_LIST, sizeof(PEER_OBJECT_LIST) - 1U), 0,
                  "unexpected object list");
}

ZTEST(sirena_lwm2m_mqtt, test_a_refused_registration_is_reported)
{
    int rc;

    k_mutex_lock(&peer_lock, K_FOREVER);
    peer.register_result = SIRENA_LWM2M_MQTT_RESULT_BAD_REQUEST;
    k_mutex_unlock(&peer_lock);

    rc = sirena_lwm2m_transport_register();
    zassert_equal(rc, -EBADMSG, "a refused registration must be reported: %d", rc);
}

ZTEST(sirena_lwm2m_mqtt, test_the_update_carries_the_lifetime)
{
    struct sirena_lwm2m_mqtt_message message;
    struct peer_publish publish;
    int rc;

    rc = sirena_lwm2m_transport_register();
    zassert_equal(rc, 0, "the registration failed: %d", rc);

    rc = sirena_lwm2m_transport_update();
    zassert_equal(rc, 0, "the update failed: %d", rc);

    zassert_true(peer_wait_publish(peer_match_update, 0U, &publish, &message),
                 "the broker received no update");
    zassert_equal(message.operation, SIRENA_LWM2M_MQTT_OPERATION_UPDATE,
                  "unexpected update operation");
    zassert_true(message.has_lifetime, "the update carries no lifetime");
    zassert_equal(message.lifetime, PEER_LIFETIME, "unexpected lifetime");
}

ZTEST(sirena_lwm2m_mqtt, test_the_deregistration_is_published)
{
    struct sirena_lwm2m_mqtt_message message;
    struct peer_publish publish;
    int rc;

    rc = sirena_lwm2m_transport_register();
    zassert_equal(rc, 0, "the registration failed: %d", rc);

    rc = sirena_lwm2m_transport_deregister();
    zassert_equal(rc, 0, "the deregistration failed: %d", rc);

    zassert_true(peer_wait_publish(peer_match_deregister, 0U, &publish, &message),
                 "the broker received no deregistration");
    zassert_equal(message.operation, SIRENA_LWM2M_MQTT_OPERATION_DEREGISTER,
                  "unexpected deregistration operation");
}

ZTEST(sirena_lwm2m_mqtt, test_a_read_is_answered_with_the_resource_value)
{
    struct sirena_lwm2m_mqtt_message request = {
        .has_operation = true,
        .operation = SIRENA_LWM2M_MQTT_OPERATION_READ,
        .token = 56U,
        .has_uri = true,
    };
    struct sirena_lwm2m_mqtt_message answer;
    struct peer_publish publish;
    int rc;

    strcpy(request.uri, "/1/0/1");

    rc = peer_publish_command(&request);
    zassert_equal(rc, 0, "the read command could not be published: %d", rc);

    zassert_true(peer_wait_publish(peer_match_result, 0U, &publish, &answer),
                 "the broker received no answer");
    zassert_str_equal(publish.topic, "lwm2m/rd/" PEER_ENDPOINT_NAME, "unexpected answer topic");
    zassert_true(answer.has_result, "the answer carries no result");
    zassert_equal(answer.result, SIRENA_LWM2M_MQTT_RESULT_CONTENT, "unexpected result");
    zassert_equal(answer.token, request.token, "unexpected token");
    zassert_true(answer.has_content_format, "the answer carries no content format");
    zassert_equal(answer.content_format, CONFIG_SIRENA_LWM2M_PREFERRED_CONTENT_FORMAT,
                  "unexpected content format");
    zassert_true(answer.has_payload, "the answer carries no value");
    zassert_equal(peer_decode_integer(&answer), PEER_LIFETIME, "unexpected resource value");
}

ZTEST(sirena_lwm2m_mqtt, test_a_read_of_an_unknown_resource_is_not_found)
{
    struct sirena_lwm2m_mqtt_message request = {
        .has_operation = true,
        .operation = SIRENA_LWM2M_MQTT_OPERATION_READ,
        .token = 57U,
        .has_uri = true,
    };
    struct sirena_lwm2m_mqtt_message answer;
    struct peer_publish publish;
    int rc;

    strcpy(request.uri, "/3/0/99");

    rc = peer_publish_command(&request);
    zassert_equal(rc, 0, "the read command could not be published: %d", rc);

    zassert_true(peer_wait_publish(peer_match_result, 0U, &publish, &answer),
                 "the broker received no answer");
    zassert_equal(answer.result, SIRENA_LWM2M_MQTT_RESULT_NOT_FOUND, "unexpected result");
    zassert_false(answer.has_payload, "a failure must not carry a value");
}

ZTEST(sirena_lwm2m_mqtt, test_a_write_updates_the_resource)
{
    struct sirena_lwm2m_mqtt_message request = {
        .has_operation = true,
        .operation = SIRENA_LWM2M_MQTT_OPERATION_WRITE,
        .token = 58U,
        .has_uri = true,
        .has_content_format = true,
        .content_format = SIRENA_LWM2M_CF_TLV,
        .has_payload = true,
    };
    struct sirena_lwm2m_mqtt_message answer;
    struct peer_publish publish;
    uint8_t value[32];
    int64_t lifetime = 0;
    size_t value_length;
    int rc;

    strcpy(request.uri, "/1/0/1");
    value_length = peer_encode_integer(PEER_SERVER_OBJECT_ID, PEER_SERVER_LIFETIME_RESOURCE,
                                       PEER_LIFETIME_UPDATED, value, sizeof(value));
    request.payload = value;
    request.payload_length = value_length;

    rc = peer_publish_command(&request);
    zassert_equal(rc, 0, "the write command could not be published: %d", rc);

    zassert_true(peer_wait_publish(peer_match_result, 0U, &publish, &answer),
                 "the broker received no answer");
    zassert_equal(answer.result, SIRENA_LWM2M_MQTT_RESULT_CHANGED, "unexpected result");
    zassert_equal(answer.token, request.token, "unexpected token");

    rc = sirena_lwm2m_get_integer(PEER_SERVER_OBJECT_ID, PEER_INSTANCE_ID,
                                   PEER_SERVER_LIFETIME_RESOURCE, &lifetime);
    zassert_equal(rc, 0, "the stored value is not readable");
    zassert_equal(lifetime, PEER_LIFETIME_UPDATED, "the stored value was not updated");
}

ZTEST(sirena_lwm2m_mqtt, test_a_write_of_an_unsupported_format_is_refused)
{
    struct sirena_lwm2m_mqtt_message request = {
        .has_operation = true,
        .operation = SIRENA_LWM2M_MQTT_OPERATION_WRITE,
        .token = 59U,
        .has_uri = true,
        .has_content_format = true,
        .content_format = SIRENA_LWM2M_CF_SENML_JSON,
        .has_payload = true,
    };
    struct sirena_lwm2m_mqtt_message answer;
    struct peer_publish publish;
    static const uint8_t value[] = { '{', '"', 'b', 'n', '"', ':', '"', '/', '1', '/', '0', '/',
                                     '"', '}' };
    int64_t lifetime = 0;
    int rc;

    strcpy(request.uri, "/1/0/1");
    request.payload = value;
    request.payload_length = sizeof(value);

    rc = peer_publish_command(&request);
    zassert_equal(rc, 0, "the write command could not be published: %d", rc);

    zassert_true(peer_wait_publish(peer_match_result, 0U, &publish, &answer),
                 "the broker received no answer");
    zassert_equal(answer.result, SIRENA_LWM2M_MQTT_RESULT_UNSUPPORTED_FORMAT, "unexpected result");

    rc = sirena_lwm2m_get_integer(PEER_SERVER_OBJECT_ID, PEER_INSTANCE_ID,
                                   PEER_SERVER_LIFETIME_RESOURCE, &lifetime);
    zassert_equal(rc, 0, "the stored value is not readable");
    zassert_equal(lifetime, PEER_LIFETIME, "the stored value was changed");
}

ZTEST(sirena_lwm2m_mqtt, test_an_execute_invokes_the_handler)
{
    struct sirena_lwm2m_mqtt_message request = {
        .has_operation = true,
        .operation = SIRENA_LWM2M_MQTT_OPERATION_EXECUTE,
        .token = 60U,
        .has_uri = true,
    };
    struct sirena_lwm2m_mqtt_message answer;
    struct peer_publish publish;
    int rc;

    strcpy(request.uri, "/3/0/4");

    rc = peer_publish_command(&request);
    zassert_equal(rc, 0, "the execute command could not be published: %d", rc);

    zassert_true(peer_wait_publish(peer_match_result, 0U, &publish, &answer),
                 "the broker received no answer");
    zassert_equal(answer.result, SIRENA_LWM2M_MQTT_RESULT_CHANGED, "unexpected result");
    zassert_true(peer_execute_called, "the execute handler was not called");
    zassert_equal(peer_execute_object_id, PEER_DEVICE_OBJECT_ID, "unexpected object id");
    zassert_equal(peer_execute_resource_id, PEER_DEVICE_REBOOT_RESOURCE,
                  "unexpected resource id");
}

ZTEST(sirena_lwm2m_mqtt, test_an_observe_notifies_the_new_value)
{
    struct sirena_lwm2m_mqtt_message request = {
        .has_operation = true,
        .operation = SIRENA_LWM2M_MQTT_OPERATION_OBSERVE,
        .token = 61U,
        .has_uri = true,
    };
    struct sirena_lwm2m_mqtt_message answer;
    struct sirena_lwm2m_mqtt_message notification;
    struct peer_publish publish;
    int rc;

    strcpy(request.uri, "/1/0/1");

    rc = peer_publish_command(&request);
    zassert_equal(rc, 0, "the observe command could not be published: %d", rc);

    zassert_true(peer_wait_publish(peer_match_result, 0U, &publish, &answer),
                 "the broker received no answer");
    zassert_equal(answer.result, SIRENA_LWM2M_MQTT_RESULT_CONTENT, "unexpected result");

    rc = sirena_lwm2m_set_integer(PEER_SERVER_OBJECT_ID, PEER_INSTANCE_ID,
                                   PEER_SERVER_LIFETIME_RESOURCE, PEER_LIFETIME_NOTIFIED);
    zassert_equal(rc, 0, "the value update failed");

    rc = sirena_lwm2m_notify_resource(PEER_SERVER_OBJECT_ID, PEER_INSTANCE_ID,
                                      PEER_SERVER_LIFETIME_RESOURCE);
    zassert_equal(rc, 1, "the notification must reach the observation: %d", rc);

    zassert_true(peer_wait_publish(peer_match_notify, 0U, &publish, &notification),
                 "the broker received no notification");
    zassert_str_equal(publish.topic, "lwm2m/rd/" PEER_ENDPOINT_NAME,
                      "unexpected notification topic");
    zassert_equal(notification.token, request.token, "unexpected notification token");
    zassert_true(notification.has_content_format, "the notification carries no content format");
    zassert_equal(notification.content_format, CONFIG_SIRENA_LWM2M_PREFERRED_CONTENT_FORMAT,
                  "unexpected content format");
    zassert_true(notification.has_payload, "the notification carries no value");
    zassert_equal(peer_decode_integer(&notification), PEER_LIFETIME_NOTIFIED,
                  "the notification carries a stale value");
}

ZTEST(sirena_lwm2m_mqtt, test_a_cancelled_observation_stops_the_notifications)
{
    struct sirena_lwm2m_mqtt_message request = {
        .has_operation = true,
        .operation = SIRENA_LWM2M_MQTT_OPERATION_OBSERVE,
        .token = 62U,
        .has_uri = true,
    };
    struct sirena_lwm2m_mqtt_message cancel = {
        .has_operation = true,
        .operation = SIRENA_LWM2M_MQTT_OPERATION_CANCEL_OBSERVE,
        .token = 62U,
    };
    struct sirena_lwm2m_mqtt_message answer;
    struct peer_publish publish;
    int rc;

    strcpy(request.uri, "/1/0/1");

    rc = peer_publish_command(&request);
    zassert_equal(rc, 0, "the observe command could not be published: %d", rc);

    zassert_true(peer_wait_publish(peer_match_result, 0U, &publish, &answer),
                 "the broker received no answer");
    zassert_equal(answer.result, SIRENA_LWM2M_MQTT_RESULT_CONTENT, "unexpected result");

    rc = peer_publish_command(&cancel);
    zassert_equal(rc, 0, "the cancel command could not be published: %d", rc);

    zassert_true(peer_wait_publish(peer_match_result, 1U, &publish, &answer),
                 "the broker received no answer to the cancellation");

    rc = sirena_lwm2m_set_integer(PEER_SERVER_OBJECT_ID, PEER_INSTANCE_ID,
                                   PEER_SERVER_LIFETIME_RESOURCE, PEER_LIFETIME_NOTIFIED);
    zassert_equal(rc, 0, "the value update failed");

    rc = sirena_lwm2m_notify_resource(PEER_SERVER_OBJECT_ID, PEER_INSTANCE_ID,
                                      PEER_SERVER_LIFETIME_RESOURCE);
    zassert_equal(rc, 0, "a cancelled observation must not be notified: %d", rc);
}

static int peer_provision(const char *uri, uint8_t security_mode)
{
    struct sirena_lwm2m_provisioning provisioning = {
        .server_uri = uri,
        .security_mode = security_mode,
        .identity = PEER_PSK_IDENTITY,
        .identity_length = sizeof(PEER_PSK_IDENTITY) - 1U,
        .secret_key = PEER_PSK_KEY,
        .secret_key_length = sizeof(PEER_PSK_KEY) - 1U,
        .short_server_id = 1U,
        .lifetime = PEER_LIFETIME,
        .binding = "M",
    };
    int rc;

    rc = sirena_lwm2m_provision(&provisioning);
    zassert_equal(rc, 0, "the provisioning failed: %d", rc);

    return rc;
}

static void peer_wait_tls_connected(void)
{
    int64_t deadline = k_uptime_get() + PEER_WAIT_MS;

    while (k_uptime_get() < deadline) {
        if (peer_tls_connected) {
            return;
        }

        k_msleep(PEER_POLL_MS);
    }

    zassert_unreachable("the broker received no connect message over TLS");
}

static void peer_wait_tls_subscribed(void)
{
    int64_t deadline = k_uptime_get() + PEER_WAIT_MS;

    while (k_uptime_get() < deadline) {
        if (peer_tls_subscribed) {
            return;
        }

        k_msleep(PEER_POLL_MS);
    }

    zassert_unreachable("the client did not subscribe over TLS");
}

ZTEST(sirena_lwm2m_mqtt, test_an_unsupported_operation_is_answered_with_not_implemented)
{
    struct sirena_lwm2m_mqtt_message request = {
        .has_operation = true,
        .operation = PEER_OPERATION_READ_COMPOSITE,
        .token = 63U,
    };
    struct sirena_lwm2m_mqtt_message answer;
    struct peer_publish publish;
    int rc;

    rc = peer_publish_command(&request);
    zassert_equal(rc, 0, "the command could not be published: %d", rc);

    zassert_true(peer_wait_publish(peer_match_result, 0U, &publish, &answer),
                 "the broker received no answer");
    zassert_equal(answer.result, SIRENA_LWM2M_MQTT_RESULT_NOT_IMPLEMENTED, "unexpected result");
    zassert_equal(answer.token, request.token, "unexpected token");
    zassert_false(answer.has_payload, "a failure must not carry a value");
}

ZTEST(sirena_lwm2m_mqtt, test_an_answer_with_another_token_is_refused)
{
    int rc;

    k_mutex_lock(&peer_lock, K_FOREVER);
    peer.result_token_delta = 1U;
    k_mutex_unlock(&peer_lock);

    rc = sirena_lwm2m_transport_register();
    zassert_equal(rc, -ETIMEDOUT, "an answer with another token must not register: %d", rc);

    k_mutex_lock(&peer_lock, K_FOREVER);
    peer.result_token_delta = 0U;
    k_mutex_unlock(&peer_lock);
}

ZTEST(sirena_lwm2m_mqtt, test_an_oversized_publish_is_refused)
{
    struct sirena_lwm2m_mqtt_message request = {
        .has_operation = true,
        .operation = SIRENA_LWM2M_MQTT_OPERATION_READ,
        .token = 64U,
        .has_uri = true,
    };
    struct sirena_lwm2m_mqtt_message answer;
    struct peer_publish publish;
    uint8_t payload[600];
    size_t length = sizeof(payload);
    int rc;

    strcpy(request.uri, "/1/0/1");

    memset(payload, 0x5AU, sizeof(payload));
    rc = sirena_lwm2m_mqtt_message_encode(&request, payload, &length);
    zassert_equal(rc, 0, "the command encoding failed: %d", rc);

    peer_wait_subscribed();

    k_mutex_lock(&peer_lock, K_FOREVER);
    rc = peer_send_publish_locked(peer.subscribe_topic, payload, sizeof(payload));
    k_mutex_unlock(&peer_lock);
    zassert_equal(rc, 0, "the oversized command could not be published: %d", rc);

    k_msleep(200);

    k_mutex_lock(&peer_lock, K_FOREVER);
    zassert_equal(peer.publish_count, 0U, "the client answered an oversized publish");
    k_mutex_unlock(&peer_lock);

    rc = peer_publish_command(&request);
    zassert_equal(rc, 0, "the read command could not be published: %d", rc);

    zassert_true(peer_wait_publish(peer_match_result, 0U, &publish, &answer),
                 "the broker received no answer");
    zassert_equal(answer.result, SIRENA_LWM2M_MQTT_RESULT_CONTENT, "unexpected result");
}

ZTEST(sirena_lwm2m_mqtt, test_a_payload_with_a_wrapping_length_is_refused)
{
    static const uint8_t payload[] = {
        0xA1U, 0x07U, 0x5BU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
        0xFFU, 0xFFU, 0xFFU, 0xFFU,
    };
    struct sirena_lwm2m_mqtt_message message;
    int rc;

    rc = sirena_lwm2m_mqtt_message_decode(payload, sizeof(payload), &message);
    zassert_not_equal(rc, 0, "a wrapping length must be refused");
}

ZTEST(sirena_lwm2m_mqtt, test_a_deeply_nested_payload_is_refused)
{
    uint8_t payload[2U + PEER_CBOR_MAX_DEPTH + 8U];
    struct sirena_lwm2m_mqtt_message message;
    size_t offset = 0U;
    size_t i;
    int rc;

    payload[offset++] = 0xA1U;
    payload[offset++] = 0x20U;

    for (i = 0U; i < PEER_CBOR_MAX_DEPTH + 4U; ++i) {
        payload[offset++] = 0x81U;
    }

    payload[offset++] = 0x00U;

    rc = sirena_lwm2m_mqtt_message_decode(payload, offset, &message);
    zassert_not_equal(rc, 0, "a deeply nested payload must be refused");
}

ZTEST(sirena_lwm2m_mqtt, test_the_secure_scheme_is_refused_for_a_nosec_server)
{
    char uri[64];
    int rc;

    rc = peer_stop_transport();
    zassert_equal(rc, 0, "the transport stop failed: %d", rc);

    rc = snprintf(uri, sizeof(uri), "mqtts://127.0.0.1:%u", peer_port);
    zassert_true(rc > 0 && rc < (int)sizeof(uri), "the server uri build failed");

    rc = peer_provision(uri, SIRENA_LWM2M_SECURITY_MODE_NOSEC);
    zassert_equal(rc, 0, "the provisioning failed: %d", rc);

    rc = peer_start_transport();
    zassert_equal(rc, -EINVAL, "a nosec server must not use the secure scheme: %d", rc);
}

ZTEST(sirena_lwm2m_mqtt, test_the_plain_scheme_is_refused_for_a_psk_server)
{
    char uri[64];
    int rc;

    rc = peer_stop_transport();
    zassert_equal(rc, 0, "the transport stop failed: %d", rc);

    rc = snprintf(uri, sizeof(uri), "mqtt://127.0.0.1:%u", peer_port);
    zassert_true(rc > 0 && rc < (int)sizeof(uri), "the server uri build failed");

    rc = peer_provision(uri, SIRENA_LWM2M_SECURITY_MODE_PSK);
    zassert_equal(rc, 0, "the provisioning failed: %d", rc);

    rc = peer_start_transport();
    zassert_equal(rc, -EINVAL, "a psk server must not use the plain scheme: %d", rc);
}

ZTEST(sirena_lwm2m_mqtt, test_the_plain_scheme_is_refused_for_a_certificate_server)
{
    char uri[64];
    int rc;

    rc = peer_stop_transport();
    zassert_equal(rc, 0, "the transport stop failed: %d", rc);

    rc = snprintf(uri, sizeof(uri), "mqtt://127.0.0.1:%u", peer_port);
    zassert_true(rc > 0 && rc < (int)sizeof(uri), "the server uri build failed");

    rc = peer_provision(uri, SIRENA_LWM2M_SECURITY_MODE_CERTIFICATE);
    zassert_equal(rc, 0, "the provisioning failed: %d", rc);

    rc = peer_start_transport();
    zassert_equal(rc, -EINVAL, "a certificate server must not use the plain scheme: %d", rc);
}

ZTEST(sirena_lwm2m_mqtt, test_the_secure_transport_connects_to_the_broker)
{
    char uri[64];
    int rc;

    rc = peer_stop_transport();
    zassert_equal(rc, 0, "the transport stop failed: %d", rc);

    rc = snprintf(uri, sizeof(uri), "mqtts://127.0.0.1:%u", peer_tls_port);
    zassert_true(rc > 0 && rc < (int)sizeof(uri), "the server uri build failed");

    rc = peer_provision(uri, SIRENA_LWM2M_SECURITY_MODE_PSK);
    zassert_equal(rc, 0, "the provisioning failed: %d", rc);

    rc = peer_start_transport();
    zassert_equal(rc, 0, "the secure transport start failed: %d", rc);

    peer_wait_tls_connected();
    peer_wait_tls_subscribed();

    zassert_str_equal(peer_tls_client_id, PEER_ENDPOINT_NAME, "unexpected client id");
    zassert_str_equal(peer_tls_subscribe_topic, "lwm2m/rd/" PEER_ENDPOINT_NAME,
                      "unexpected subscription topic");

    rc = peer_stop_transport();
    zassert_equal(rc, 0, "the transport stop failed: %d", rc);
}
