#include "sirena_lwm2m_coap_endpoint.h"
#include <sirena/lwm2m/protocol.h>
#include <zephyr/kernel.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <string.h>

#define SIRENA_LWM2M_COAP_PATH_SEGMENTS 3
#define SIRENA_LWM2M_COAP_PATH_OPTIONS 4
#define SIRENA_LWM2M_COAP_OPTIONS 16
#define SIRENA_LWM2M_COAP_TOKEN_LENGTH 4

struct sirena_lwm2m_coap_observer {
    bool used;
    uint8_t token[COAP_TOKEN_MAX_LEN];
    uint8_t token_length;
    uint16_t object_id;
    uint16_t instance_id;
    uint16_t resource_id;
    uint16_t content_format;
    uint32_t sequence;
    struct net_sockaddr_storage peer;
    net_socklen_t peer_length;
};

struct sirena_lwm2m_coap_exchange {
    bool active;
    bool separated;
    bool responded;
    bool reset;
    uint8_t token[COAP_TOKEN_MAX_LEN];
    uint8_t token_length;
    struct coap_packet request;
    uint8_t request_buffer[CONFIG_SIRENA_LWM2M_COAP_MESSAGE_MAX];
    struct coap_pending pending;
    uint8_t response_buffer[CONFIG_SIRENA_LWM2M_COAP_MESSAGE_MAX];
    struct coap_packet response;
    struct coap_option response_options[SIRENA_LWM2M_COAP_OPTIONS];
    uint16_t response_code;
};

static int endpoint_socket = -1;
static struct net_sockaddr_storage endpoint_server;
static net_socklen_t endpoint_server_length;
static struct sirena_lwm2m_coap_exchange exchange;
static struct sirena_lwm2m_coap_observer observers[CONFIG_SIRENA_LWM2M_MAX_OBSERVATIONS];
static uint8_t receive_buffer[CONFIG_SIRENA_LWM2M_COAP_MESSAGE_MAX];
static uint8_t response_payload[CONFIG_SIRENA_LWM2M_PROTOCOL_PAYLOAD_MAX];
static uint8_t notify_payload[CONFIG_SIRENA_LWM2M_PROTOCOL_PAYLOAD_MAX];

static uint8_t endpoint_status_code(enum sirena_lwm2m_status status)
{
    switch (status) {
    case SIRENA_LWM2M_STATUS_CHANGED:
        return COAP_RESPONSE_CODE_CHANGED;
    case SIRENA_LWM2M_STATUS_CONTENT:
        return COAP_RESPONSE_CODE_CONTENT;
    case SIRENA_LWM2M_STATUS_BAD_REQUEST:
        return COAP_RESPONSE_CODE_BAD_REQUEST;
    case SIRENA_LWM2M_STATUS_NOT_FOUND:
        return COAP_RESPONSE_CODE_NOT_FOUND;
    case SIRENA_LWM2M_STATUS_METHOD_NOT_ALLOWED:
        return COAP_RESPONSE_CODE_NOT_ALLOWED;
    case SIRENA_LWM2M_STATUS_NOT_ACCEPTABLE:
        return COAP_RESPONSE_CODE_NOT_ACCEPTABLE;
    case SIRENA_LWM2M_STATUS_UNSUPPORTED_CONTENT_FORMAT:
        return COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT;
    default:
        return COAP_RESPONSE_CODE_INTERNAL_ERROR;
    }
}

static int endpoint_send_message(uint8_t type, uint16_t message_id, uint8_t token_length,
                                  const uint8_t *token, uint8_t code, uint16_t content_format,
                                  int observe_sequence, const void *payload,
                                  size_t payload_length, const struct net_sockaddr *peer,
                                  net_socklen_t peer_length)
{
    struct coap_packet response;
    uint8_t buffer[CONFIG_SIRENA_LWM2M_COAP_MESSAGE_MAX];
    int rc;

    if (type == COAP_TYPE_NON_CON) {
        message_id = coap_next_id();
    }

    rc = coap_packet_init(&response, buffer, sizeof(buffer), COAP_VERSION_1, type, token_length,
                           token, code, message_id);
    if (rc < 0) {
        return rc;
    }

    if (content_format != 0U) {
        rc = coap_append_option_int(&response, COAP_OPTION_CONTENT_FORMAT, content_format);
        if (rc < 0) {
            return rc;
        }
    }

    if (observe_sequence >= 0) {
        rc = coap_append_option_int(&response, COAP_OPTION_OBSERVE,
                                    (unsigned int)observe_sequence);
        if (rc < 0) {
            return rc;
        }
    }

    if (payload != NULL && payload_length > 0U) {
        rc = coap_packet_append_payload_marker(&response);
        if (rc < 0) {
            return rc;
        }

        rc = coap_packet_append_payload(&response, payload, (uint16_t)payload_length);
        if (rc < 0) {
            return rc;
        }
    }

    if (zsock_sendto(endpoint_socket, response.data, response.offset, 0, peer, peer_length) < 0) {
        return -errno;
    }

    return 0;
}

static void endpoint_send_empty_ack(const struct coap_packet *request,
                                     const struct net_sockaddr *peer, net_socklen_t peer_length)
{
    struct coap_packet ack;
    uint8_t buffer[COAP_TOKEN_MAX_LEN + 4U];

    if (coap_ack_init(&ack, request, buffer, sizeof(buffer), COAP_CODE_EMPTY) < 0) {
        return;
    }

    (void)zsock_sendto(endpoint_socket, ack.data, ack.offset, 0, peer, peer_length);
}

static struct sirena_lwm2m_coap_observer *endpoint_observer_find(const uint8_t *token,
                                                                  uint8_t token_length,
                                                                  const struct net_sockaddr *peer)
{
    size_t i;

    for (i = 0U; i < ARRAY_SIZE(observers); ++i) {
        struct sirena_lwm2m_coap_observer *observer = &observers[i];

        if (!observer->used) {
            continue;
        }

        if (observer->token_length != token_length) {
            continue;
        }

        if (memcmp(observer->token, token, token_length) != 0) {
            continue;
        }

        if (peer != NULL && net_sockaddr_cmp(net_sad(&observer->peer), peer) != 0) {
            continue;
        }

        return observer;
    }

    return NULL;
}

static struct sirena_lwm2m_coap_observer *endpoint_observer_alloc(void)
{
    size_t i;

    for (i = 0U; i < ARRAY_SIZE(observers); ++i) {
        if (!observers[i].used) {
            return &observers[i];
        }
    }

    return NULL;
}

size_t sirena_lwm2m_coap_endpoint_observation_count(void)
{
    size_t count = 0U;
    size_t i;

    for (i = 0U; i < ARRAY_SIZE(observers); ++i) {
        if (observers[i].used) {
            count++;
        }
    }

    return count;
}

static bool endpoint_token_matches(const struct coap_packet *packet)
{
    uint8_t token[COAP_TOKEN_MAX_LEN];
    uint8_t token_length = coap_header_get_token(packet, token);

    if (token_length != exchange.token_length) {
        return false;
    }

    return memcmp(token, exchange.token, token_length) == 0;
}

static void endpoint_complete_response(const struct coap_packet *packet)
{
    memcpy(exchange.response_buffer, packet->data, packet->offset);
    (void)coap_packet_parse(&exchange.response, exchange.response_buffer, packet->offset,
                            exchange.response_options, ARRAY_SIZE(exchange.response_options));
    exchange.response_code = coap_header_get_code(packet);
    exchange.responded = true;
    coap_pending_clear(&exchange.pending);
}

static void endpoint_handle_response(struct coap_packet *packet, const struct net_sockaddr *peer,
                                     net_socklen_t peer_length)
{
    uint8_t code = coap_header_get_code(packet);
    uint16_t message_id = coap_header_get_id(packet);

    if (!exchange.active) {
        return;
    }

    if (exchange.separated) {
        if (!endpoint_token_matches(packet)) {
            return;
        }

        if (coap_header_get_type(packet) == COAP_TYPE_CON) {
            endpoint_send_empty_ack(packet, peer, peer_length);
        }

        endpoint_complete_response(packet);
        return;
    }

    if (message_id != exchange.pending.id) {
        return;
    }

    if (coap_header_get_type(packet) == COAP_TYPE_RESET) {
        exchange.reset = true;
        exchange.responded = true;
        coap_pending_clear(&exchange.pending);
        return;
    }

    if (code == COAP_CODE_EMPTY) {
        exchange.separated = true;
        coap_pending_clear(&exchange.pending);
        return;
    }

    endpoint_complete_response(packet);
}

static size_t endpoint_parse_path(const struct coap_packet *packet, uint16_t *path)
{
    struct coap_option options[SIRENA_LWM2M_COAP_PATH_OPTIONS];
    size_t count = 0U;
    int found;
    int i;

    found = coap_find_options(packet, COAP_OPTION_URI_PATH, options, ARRAY_SIZE(options));
    if (found <= 0) {
        return 0U;
    }

    for (i = 0; i < found; ++i) {
        uint32_t value = 0U;
        uint16_t j;

        if (options[i].len == 0U || options[i].len > 5U) {
            return 0U;
        }

        for (j = 0U; j < options[i].len; ++j) {
            if (options[i].value[j] < '0' || options[i].value[j] > '9') {
                return 0U;
            }

            value = value * 10U + (uint32_t)(options[i].value[j] - '0');
        }

        path[count++] = (uint16_t)value;
    }

    return count;
}

static size_t endpoint_parse_accept(const struct coap_packet *packet, uint16_t *accepted,
                                    size_t capacity)
{
    struct coap_option options[CONFIG_SIRENA_LWM2M_ACCEPTED_FORMATS_MAX];
    size_t count = 0U;
    int found;
    int i;

    found = coap_find_options(packet, COAP_OPTION_ACCEPT, options, ARRAY_SIZE(options));
    if (found <= 0) {
        return 0U;
    }

    for (i = 0; i < found && count < capacity; ++i) {
        accepted[count++] = (uint16_t)coap_option_value_to_int(&options[i]);
    }

    return count;
}

static void endpoint_handle_request(struct coap_packet *packet, const struct net_sockaddr *peer,
                                    net_socklen_t peer_length)
{
    struct sirena_lwm2m_protocol_response protocol_response;
    struct sirena_lwm2m_protocol_request protocol_request;
    struct sirena_lwm2m_coap_observer *observer = NULL;
    uint16_t accepted[CONFIG_SIRENA_LWM2M_ACCEPTED_FORMATS_MAX];
    uint16_t path[SIRENA_LWM2M_COAP_PATH_SEGMENTS];
    uint8_t token[COAP_TOKEN_MAX_LEN];
    uint16_t message_id = coap_header_get_id(packet);
    const uint8_t *payload;
    uint16_t payload_length = 0U;
    uint8_t token_length = coap_header_get_token(packet, token);
    size_t accepted_count;
    size_t path_count;
    uint8_t request_type = coap_header_get_type(packet);
    uint8_t response_type;
    int content_format;
    int observe;
    uint8_t code;

    response_type = request_type == COAP_TYPE_CON ? COAP_TYPE_ACK : COAP_TYPE_NON_CON;

    path_count = endpoint_parse_path(packet, path);
    if (path_count != SIRENA_LWM2M_COAP_PATH_SEGMENTS) {
        (void)endpoint_send_message(response_type, message_id, token_length, token,
                                    COAP_RESPONSE_CODE_NOT_FOUND, 0U, -1, NULL, 0U, peer,
                                    peer_length);
        return;
    }

    observe = coap_get_option_int(packet, COAP_OPTION_OBSERVE);
    content_format = coap_get_option_int(packet, COAP_OPTION_CONTENT_FORMAT);
    accepted_count = endpoint_parse_accept(packet, accepted, ARRAY_SIZE(accepted));
    payload = coap_packet_get_payload(packet, &payload_length);

    switch (coap_header_get_code(packet)) {
    case COAP_METHOD_GET:
        protocol_request.operation = SIRENA_LWM2M_OPERATION_READ;
        break;
    case COAP_METHOD_PUT:
        protocol_request.operation = SIRENA_LWM2M_OPERATION_WRITE;
        break;
    case COAP_METHOD_POST:
        protocol_request.operation = SIRENA_LWM2M_OPERATION_EXECUTE;
        break;
    default:
        (void)endpoint_send_message(response_type, message_id, token_length, token,
                                    COAP_RESPONSE_CODE_NOT_ALLOWED, 0U, -1, NULL, 0U, peer,
                                    peer_length);
        return;
    }

    if (protocol_request.operation == SIRENA_LWM2M_OPERATION_READ && observe == 0) {
        observer = endpoint_observer_find(token, token_length, peer);
        if (observer == NULL) {
            observer = endpoint_observer_alloc();
        }

        if (observer == NULL) {
            (void)endpoint_send_message(response_type, message_id, token_length, token,
                                        COAP_RESPONSE_CODE_SERVICE_UNAVAILABLE, 0U, -1, NULL, 0U,
                                        peer, peer_length);
            return;
        }
    }

    protocol_request.object_id = path[0];
    protocol_request.instance_id = path[1];
    protocol_request.resource_id = path[2];
    protocol_request.payload = payload;
    protocol_request.payload_length = payload_length;
    protocol_request.content_format_present = content_format >= 0;
    protocol_request.content_format = content_format >= 0 ? (uint16_t)content_format : 0U;
    protocol_request.accepted_formats = accepted;
    protocol_request.accepted_format_count = accepted_count;

    protocol_response.payload = response_payload;
    protocol_response.payload_capacity = sizeof(response_payload);
    protocol_response.payload_length = 0U;
    protocol_response.content_format = 0U;
    protocol_response.status = SIRENA_LWM2M_STATUS_INTERNAL_ERROR;

    (void)sirena_lwm2m_protocol_execute(&protocol_request, &protocol_response);

    code = endpoint_status_code(protocol_response.status);

    if (observer != NULL && code == COAP_RESPONSE_CODE_CONTENT) {
        observer->used = true;
        observer->token_length = token_length;
        memcpy(observer->token, token, token_length);
        observer->object_id = path[0];
        observer->instance_id = path[1];
        observer->resource_id = path[2];
        observer->content_format = protocol_response.content_format;
        observer->sequence = 0U;
        memcpy(&observer->peer, peer, sizeof(observer->peer));
        observer->peer_length = peer_length;
    }

    if (protocol_request.operation == SIRENA_LWM2M_OPERATION_READ && observe == 1) {
        struct sirena_lwm2m_coap_observer *cancelled;

        cancelled = endpoint_observer_find(token, token_length, peer);
        if (cancelled != NULL) {
            cancelled->used = false;
        }
    }

    (void)endpoint_send_message(response_type, message_id, token_length, token, code,
                                protocol_response.content_format,
                                observer != NULL ? (int)observer->sequence : -1,
                                protocol_response.payload, protocol_response.payload_length, peer,
                                peer_length);
}

static int endpoint_dispatch(uint8_t *data, size_t length, const struct net_sockaddr *peer,
                             net_socklen_t peer_length)
{
    struct coap_option options[SIRENA_LWM2M_COAP_OPTIONS];
    struct coap_packet packet;

    if (coap_packet_parse(&packet, data, (uint16_t)length, options, ARRAY_SIZE(options)) < 0) {
        return -EINVAL;
    }

    if (coap_packet_is_request(&packet)) {
        endpoint_handle_request(&packet, peer, peer_length);
    } else {
        endpoint_handle_response(&packet, peer, peer_length);
    }

    return 0;
}

static int endpoint_receive(void)
{
    struct net_sockaddr_storage peer;
    net_socklen_t peer_length = sizeof(peer);
    ssize_t received;

    received = zsock_recvfrom(endpoint_socket, receive_buffer, sizeof(receive_buffer),
                              ZSOCK_MSG_DONTWAIT, net_sad(&peer), &peer_length);
    if (received < 0) {
        return -errno;
    }

    return endpoint_dispatch(receive_buffer, (size_t)received, net_sad(&peer), peer_length);
}

int sirena_lwm2m_coap_endpoint_start(int socket)
{
    if (socket < 0) {
        return -EINVAL;
    }

    endpoint_socket = socket;

    memset(observers, 0, sizeof(observers));
    memset(&exchange, 0, sizeof(exchange));

    return 0;
}

void sirena_lwm2m_coap_endpoint_stop(void)
{
    endpoint_socket = -1;
    memset(observers, 0, sizeof(observers));
    memset(&exchange, 0, sizeof(exchange));
}

bool sirena_lwm2m_coap_endpoint_is_started(void)
{
    return endpoint_socket >= 0;
}

int sirena_lwm2m_coap_endpoint_poll(int timeout_ms)
{
    struct zsock_pollfd fds;
    int rc;

    if (endpoint_socket < 0) {
        return 0;
    }

    fds.fd = endpoint_socket;
    fds.events = ZSOCK_POLLIN;
    fds.revents = 0;

    rc = zsock_poll(&fds, 1, timeout_ms);
    if (rc < 0) {
        return -errno;
    }

    if (rc == 0 || (fds.revents & ZSOCK_POLLIN) == 0) {
        return 0;
    }

    return endpoint_receive();
}

static void endpoint_extract_location(char *location, size_t capacity)
{
    struct coap_option options[CONFIG_SIRENA_LWM2M_COAP_LOCATION_SEGMENTS];
    size_t length = 0U;
    int found;
    int i;

    if (location == NULL || capacity == 0U) {
        return;
    }

    location[0] = '\0';

    found = coap_find_options(&exchange.response, COAP_OPTION_LOCATION_PATH, options,
                              ARRAY_SIZE(options));
    for (i = 0; i < found; ++i) {
        if (length + options[i].len + 2U > capacity) {
            break;
        }

        location[length++] = '/';
        memcpy(&location[length], options[i].value, options[i].len);
        length += options[i].len;
    }

    if (length < capacity) {
        location[length] = '\0';
    }
}

int sirena_lwm2m_coap_endpoint_request(const struct net_sockaddr *server,
                                       net_socklen_t server_length, const char *path,
                                       enum coap_method method, uint16_t content_format,
                                       const void *payload, size_t payload_length,
                                       struct sirena_lwm2m_coap_response *response,
                                       char *location, size_t location_capacity)
{
    int64_t deadline;
    int64_t expiry;
    int rc;

    if (endpoint_socket < 0) {
        return -ENOTSUP;
    }

    if (path == NULL || response == NULL || server == NULL || server_length == 0U) {
        return -EINVAL;
    }

    if (server_length > sizeof(endpoint_server)) {
        return -EINVAL;
    }

    memset(&endpoint_server, 0, sizeof(endpoint_server));
    memcpy(&endpoint_server, server, server_length);
    endpoint_server_length = server_length;

    memset(&exchange, 0, sizeof(exchange));

    exchange.token_length = SIRENA_LWM2M_COAP_TOKEN_LENGTH;
    memcpy(exchange.token, coap_next_token(), exchange.token_length);

    rc = coap_packet_init(&exchange.request, exchange.request_buffer,
                           sizeof(exchange.request_buffer), COAP_VERSION_1, COAP_TYPE_CON,
                           exchange.token_length, exchange.token, method, coap_next_id());
    if (rc < 0) {
        return rc;
    }

    rc = coap_packet_set_path(&exchange.request, path);
    if (rc < 0) {
        return rc;
    }

    if (payload != NULL && payload_length > 0U) {
        if (content_format != 0U) {
            rc = coap_append_option_int(&exchange.request, COAP_OPTION_CONTENT_FORMAT,
                                        content_format);
            if (rc < 0) {
                return rc;
            }
        }

        rc = coap_packet_append_payload_marker(&exchange.request);
        if (rc < 0) {
            return rc;
        }

        rc = coap_packet_append_payload(&exchange.request, payload, (uint16_t)payload_length);
        if (rc < 0) {
            return rc;
        }
    }

    rc = coap_pending_init(&exchange.pending, &exchange.request, net_sad(&endpoint_server), NULL);
    if (rc < 0) {
        return rc;
    }

    (void)coap_pending_cycle(&exchange.pending);

    exchange.active = true;

    if (zsock_sendto(endpoint_socket, exchange.request.data, exchange.request.offset, 0,
                     net_sad(&endpoint_server), endpoint_server_length) < 0) {
        exchange.active = false;
        return -errno;
    }

    deadline = k_uptime_get() + CONFIG_SIRENA_LWM2M_COAP_RESPONSE_TIMEOUT_MS;

    while (!exchange.responded) {
        int64_t now = k_uptime_get();
        int64_t remaining = deadline - now;
        int timeout;

        if (remaining <= 0) {
            exchange.active = false;
            return -ETIMEDOUT;
        }

        expiry = exchange.pending.timeout > 0U ? exchange.pending.t0 + exchange.pending.timeout
                                               : deadline;

        timeout = (int)MIN(remaining, expiry - now);
        if (timeout < 0) {
            timeout = 0;
        }

        (void)sirena_lwm2m_coap_endpoint_poll(timeout);

        if (exchange.responded) {
            break;
        }

        now = k_uptime_get();
        if (exchange.pending.timeout > 0U &&
            now >= exchange.pending.t0 + exchange.pending.timeout) {
            if (!coap_pending_cycle(&exchange.pending)) {
                exchange.active = false;
                return -ETIMEDOUT;
            }

            if (zsock_sendto(endpoint_socket, exchange.request.data, exchange.request.offset, 0,
                             net_sad(&endpoint_server), endpoint_server_length) < 0) {
                exchange.active = false;
                return -errno;
            }
        }
    }

    exchange.active = false;

    if (exchange.reset) {
        return -ECONNRESET;
    }

    response->code = exchange.response_code;
    response->content_format = 0U;
    response->payload = NULL;
    response->payload_length = 0U;

    {
        const uint8_t *data;
        uint16_t data_length = 0U;

        data = coap_packet_get_payload(&exchange.response, &data_length);
        if (data != NULL) {
            response->payload = data;
            response->payload_length = data_length;
        }
    }

    endpoint_extract_location(location, location_capacity);

    return 0;
}

int sirena_lwm2m_coap_endpoint_notify(uint16_t object_id, uint16_t instance_id,
                                      uint16_t resource_id)
{
    size_t i;
    int sent = 0;

    if (endpoint_socket < 0) {
        return -ENOTSUP;
    }

    for (i = 0U; i < ARRAY_SIZE(observers); ++i) {
        struct sirena_lwm2m_coap_observer *observer = &observers[i];
        size_t length = sizeof(notify_payload);
        int rc;

        if (!observer->used) {
            continue;
        }

        if (observer->object_id != object_id || observer->instance_id != instance_id ||
            observer->resource_id != resource_id) {
            continue;
        }

        rc = sirena_lwm2m_protocol_read(object_id, instance_id, resource_id,
                                        observer->content_format, notify_payload, &length);
        if (rc != 0) {
            continue;
        }

        observer->sequence++;

        if (endpoint_send_message(COAP_TYPE_NON_CON, 0U, observer->token_length, observer->token,
                                  COAP_RESPONSE_CODE_CONTENT, observer->content_format,
                                  (int)observer->sequence, notify_payload, length,
                                  net_sad(&observer->peer), observer->peer_length) == 0) {
            sent++;
        }
    }

    return sent;
}
