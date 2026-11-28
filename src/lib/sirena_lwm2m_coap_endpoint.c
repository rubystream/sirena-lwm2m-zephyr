#include "sirena_lwm2m_coap_endpoint.h"
#include <sirena/lwm2m/protocol.h>
#include <sirena/lwm2m/codec.h>
#include <zephyr/kernel.h>
#include <sirena/lwm2m/object_registry.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define SIRENA_LWM2M_COAP_PATH_SEGMENTS 3
#define SIRENA_LWM2M_COAP_PATH_OPTIONS (SIRENA_LWM2M_COAP_PATH_SEGMENTS + 1)
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
    int64_t last_notify_ms;
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
static uint8_t send_buffer[CONFIG_SIRENA_LWM2M_COAP_MESSAGE_MAX];
static uint8_t response_payload[CONFIG_SIRENA_LWM2M_PROTOCOL_PAYLOAD_MAX];
static uint8_t composite_raw[CONFIG_SIRENA_LWM2M_CODEC_MAX_VALUES][CONFIG_SIRENA_LWM2M_PROTOCOL_VALUE_MAX];
static uint8_t notify_payload[CONFIG_SIRENA_LWM2M_PROTOCOL_PAYLOAD_MAX];
static struct coap_option dispatch_options[SIRENA_LWM2M_COAP_OPTIONS];

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
                                  const uint8_t *token, uint8_t code,
                                  bool content_format_present, uint16_t content_format,
                                  int observe_sequence, const void *payload,
                                  size_t payload_length, const struct net_sockaddr *peer,
                                  net_socklen_t peer_length)
{
    struct coap_packet response;
    int rc;

    rc = coap_packet_init(&response, send_buffer, sizeof(send_buffer), COAP_VERSION_1, type,
                           token_length, token, code, message_id);
    if (rc < 0) {
        return rc;
    }

    if (content_format_present) {
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

        if (peer != NULL && !net_sockaddr_cmp(net_sad(&observer->peer), peer)) {
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

    if (!net_sockaddr_cmp(net_sad(&endpoint_server), peer)) {
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

int sirena_lwm2m_coap_endpoint_parse_path(const struct coap_packet *packet, uint16_t *path,
                                           size_t capacity, size_t *count)
{
    struct coap_option options[SIRENA_LWM2M_COAP_PATH_OPTIONS];
    size_t parsed = 0U;
    int found;
    int i;

    if (packet == NULL || path == NULL || count == NULL || capacity == 0U) {
        return -EINVAL;
    }

    found = coap_find_options(packet, COAP_OPTION_URI_PATH, options, ARRAY_SIZE(options));
    if (found < 0) {
        return -EINVAL;
    }

    if ((size_t)found > capacity) {
        return -ENOMEM;
    }

    for (i = 0; i < found; ++i) {
        uint32_t value = 0U;
        uint16_t j;

        if (options[i].len == 0U || options[i].len > 5U) {
            return -EINVAL;
        }

        for (j = 0U; j < options[i].len; ++j) {
            if (options[i].value[j] < '0' || options[i].value[j] > '9') {
                return -EINVAL;
            }

            value = value * 10U + (uint32_t)(options[i].value[j] - '0');
        }

        if (parsed >= capacity) {
            return -ENOMEM;
        }

        path[parsed++] = (uint16_t)value;
    }

    *count = parsed;

    return 0;
}

static int endpoint_parse_accept(const struct coap_packet *packet, uint16_t *accepted,
                                 size_t capacity, size_t *count)
{
    struct coap_option options[CONFIG_SIRENA_LWM2M_ACCEPTED_FORMATS_MAX + 1];
    size_t parsed = 0U;
    int found;
    int i;

    *count = 0U;

    found = coap_find_options(packet, COAP_OPTION_ACCEPT, options, ARRAY_SIZE(options));
    if (found < 0) {
        return -EINVAL;
    }

    if ((size_t)found > capacity) {
        return -ENOMEM;
    }

    for (i = 0; i < found; ++i) {
        accepted[parsed++] = (uint16_t)coap_option_value_to_int(&options[i]);
    }

    *count = parsed;

    return 0;
}


static int composite_parse_path_text(const char *text, size_t length, uint16_t *object_id,
                                     uint16_t *instance_id, uint16_t *resource_id)
{
    size_t pos = 0U;
    unsigned long parts[3] = {0, 0, 0};
    size_t part = 0U;
    unsigned long value = 0U;
    bool any = false;

    while (pos <= length) {
        if (pos == length || text[pos] == '/') {
            if (any) {
                if (part >= 3U) {
                    return -ENOTSUP;
                }
                parts[part++] = value;
                value = 0U;
                any = false;
            }
        } else {
            if (text[pos] < '0' || text[pos] > '9') {
                return -EINVAL;
            }
            value = value * 10UL + (unsigned long)(text[pos] - '0');
            if (value > 65535UL) {
                return -EINVAL;
            }
            any = true;
        }
        pos++;
    }

    if (part != 3U) {
        return -ENOTSUP;
    }

    *object_id = (uint16_t)parts[0];
    *instance_id = (uint16_t)parts[1];
    *resource_id = (uint16_t)parts[2];

    return 0;
}

static size_t composite_collect_json(const char *buf, size_t len, uint16_t *oids,
                                     uint16_t *iids, uint16_t *rids, size_t capacity,
                                     bool *invalid, bool *unknown)
{
    size_t count = 0U;
    size_t pos = 0U;
    char base[128];
    base[0] = '\0';

    while (pos < len) {
        size_t key_pos = pos;
        bool found = false;
        bool is_base = false;

        while (key_pos + 2U < len) {
            if (buf[key_pos] == '"' && buf[key_pos + 1U] == 'b' &&
                buf[key_pos + 2U] == 'n' && key_pos + 3U < len && buf[key_pos + 3U] == '"') {
                found = true;
                is_base = true;
                break;
            }
            if (buf[key_pos] == '"' && buf[key_pos + 1U] == 'n' &&
                buf[key_pos + 2U] == '"') {
                found = true;
                is_base = false;
                break;
            }
            key_pos++;
        }

        if (!found) {
            break;
        }

        {
            size_t after = is_base ? key_pos + 4U : key_pos + 3U;
            size_t quote_start;
            size_t quote_end;

            while (after < len && (buf[after] == ' ' || buf[after] == '\t' ||
                   buf[after] == '\n' || buf[after] == '\r')) {
                after++;
            }
            if (after >= len || buf[after] != ':') {
                pos = key_pos + (is_base ? 4U : 3U);
                continue;
            }
            after++;
            while (after < len && (buf[after] == ' ' || buf[after] == '\t' ||
                   buf[after] == '\n' || buf[after] == '\r')) {
                after++;
            }
            if (after >= len || buf[after] != '"') {
                pos = after;
                continue;
            }
            quote_start = after + 1U;
            quote_end = quote_start;
            while (quote_end < len && buf[quote_end] != '"') {
                quote_end++;
            }
            if (quote_end >= len) {
                break;
            }

            if (is_base) {
                size_t bl = quote_end - quote_start;
                if (bl < sizeof(base)) {
                    memcpy(base, buf + quote_start, bl);
                    base[bl] = '\0';
                }
            } else if (count <= capacity) {
                char target[256];
                size_t tlen = 0U;

                if (base[0] != '\0' && !(quote_start < len && buf[quote_start] == '/')) {
                    size_t blen = strlen(base);
                    if (blen + (quote_end - quote_start) < sizeof(target)) {
                        memcpy(target, base, blen);
                        tlen = blen;
                    }
                }
                if (tlen + (quote_end - quote_start) < sizeof(target)) {
                    memcpy(target + tlen, buf + quote_start, quote_end - quote_start);
                    tlen += (quote_end - quote_start);
                    target[tlen] = '\0';
                } else {
                    target[0] = '\0';
                    tlen = 0U;
                }

                if (count == capacity) {
                    count = capacity + 1U;
                    break;
                }
                {
                    int path_rc = tlen == 0U
                                      ? -EINVAL
                                      : composite_parse_path_text(target, tlen, &oids[count],
                                                                  &iids[count], &rids[count]);

                    if (path_rc == -ENOTSUP) {
                        *unknown = true;
                    } else if (path_rc != 0) {
                        *invalid = true;
                    } else {
                        count++;
                    }
                }
            }
            pos = quote_end + 1U;
        }
    }

    return count;
}

static size_t composite_collect_cbor(const uint8_t *buf, size_t len, uint16_t *oids,
                                     uint16_t *iids, uint16_t *rids, size_t capacity,
                                     bool *invalid, bool *unknown)
{
    size_t count = 0U;
    size_t i = 0U;
    char base[128];
    base[0] = '\0';

    while (i + 2U < len) {
        bool is_base = false;
        bool is_name = false;

        if (buf[i] == 0x62U && buf[i + 1U] == (uint8_t)'b' && buf[i + 2U] == (uint8_t)'n') {
            is_base = true;
        } else if (buf[i] == 0x61U && buf[i + 1U] == (uint8_t)'n') {
            is_name = true;
        }

        if (!is_base && !is_name) {
            i++;
            continue;
        }

        {
            size_t j = is_base ? i + 3U : i + 2U;
            size_t str_len = 0U;
            size_t str_start = 0U;

            if (j >= len) {
                break;
            }
            if (buf[j] >= 0x60U && buf[j] <= 0x77U) {
                str_len = (size_t)(buf[j] - 0x60U);
                str_start = j + 1U;
            } else if (buf[j] == 0x78U && j + 1U < len) {
                str_len = buf[j + 1U];
                str_start = j + 2U;
            } else if (buf[j] == 0x79U && j + 2U < len) {
                str_len = ((size_t)buf[j + 1U] << 8) | buf[j + 2U];
                str_start = j + 3U;
            }

            if (str_len > 0U && str_start + str_len <= len) {
                if (is_base) {
                    if (str_len < sizeof(base)) {
                        memcpy(base, buf + str_start, str_len);
                        base[str_len] = '\0';
                    }
                } else {
                    if (count == capacity) {
                        count = capacity + 1U;
                        i = str_start + str_len;
                        if (i <= j) {
                            i = j + 1U;
                        }
                        continue;
                    }
                    if (count < capacity) {
                        char target[256];
                        size_t tlen = 0U;

                        if (base[0] != '\0' && buf[str_start] != (uint8_t)'/') {
                            size_t blen = strlen(base);
                            if (blen + str_len < sizeof(target)) {
                                memcpy(target, base, blen);
                                tlen = blen;
                            }
                        }
                        if (tlen + str_len < sizeof(target)) {
                            memcpy(target + tlen, buf + str_start, str_len);
                            tlen += str_len;
                            target[tlen] = '\0';
                        } else {
                            target[0] = '\0';
                            tlen = 0U;
                        }

                        {
                            int path_rc = tlen == 0U
                                              ? -EINVAL
                                              : composite_parse_path_text(target, tlen,
                                                                          &oids[count], &iids[count],
                                                                          &rids[count]);

                            if (path_rc == -ENOTSUP) {
                                *unknown = true;
                            } else if (path_rc != 0) {
                                *invalid = true;
                            } else {
                                count++;
                            }
                        }
                    }
                }
            }
            i = str_start + str_len;
            if (i <= j) {
                i = j + 1U;
            }
            continue;
        }
    }

    return count;
}

static void endpoint_handle_request(struct coap_packet *packet, const struct net_sockaddr *peer,
                                    net_socklen_t peer_length)
{
    struct sirena_lwm2m_protocol_response protocol_response;
    struct sirena_lwm2m_protocol_request protocol_request = {0};
    struct sirena_lwm2m_coap_observer *observer = NULL;
    uint16_t accepted[CONFIG_SIRENA_LWM2M_ACCEPTED_FORMATS_MAX];
    uint16_t path[SIRENA_LWM2M_COAP_PATH_SEGMENTS];
    uint8_t token[COAP_TOKEN_MAX_LEN];
    uint16_t message_id = coap_header_get_id(packet);
    uint16_t response_message_id;
    const uint8_t *payload;
    uint16_t payload_length = 0U;
    uint8_t token_length = coap_header_get_token(packet, token);
    size_t accepted_count = 0U;
    size_t path_count = 0U;
    uint8_t request_type = coap_header_get_type(packet);
    uint8_t response_type;
    int content_format;
    struct sirena_lwm2m_resource_attributes request_attributes = {0};
    bool write_attributes = false;
    bool unknown_query = false;
    int attr_rc = 0;
    int observe;
    int rc;
    uint8_t code;

    response_type = request_type == COAP_TYPE_CON ? COAP_TYPE_ACK : COAP_TYPE_NON_CON;
    response_message_id = request_type == COAP_TYPE_CON ? message_id : coap_next_id();

    rc = sirena_lwm2m_coap_endpoint_parse_path(packet, path, ARRAY_SIZE(path), &path_count);
    if (rc != 0 || path_count == 0U || path_count > SIRENA_LWM2M_COAP_PATH_SEGMENTS) {
        (void)endpoint_send_message(response_type, response_message_id, token_length, token,
                                    COAP_RESPONSE_CODE_NOT_FOUND, false, 0U, -1, NULL, 0U, peer,
                                    peer_length);
        return;
    }

    if (coap_header_get_code(packet) == COAP_METHOD_GET ||
        coap_header_get_code(packet) == COAP_METHOD_FETCH ||
        coap_header_get_code(packet) == COAP_METHOD_IPATCH) {
        rc = endpoint_parse_accept(packet, accepted, ARRAY_SIZE(accepted), &accepted_count);
    }

    if (path_count != SIRENA_LWM2M_COAP_PATH_SEGMENTS) {
        bool allowed = false;

        if (coap_header_get_code(packet) == COAP_METHOD_GET) {
            if (rc == 0 && accepted_count > 0U &&
                accepted[0] == COAP_CONTENT_FORMAT_APP_LINK_FORMAT) {
                allowed = true;
            }
        }

        if (coap_header_get_code(packet) == COAP_METHOD_FETCH ||
            coap_header_get_code(packet) == COAP_METHOD_IPATCH) {
            allowed = true;
        }

        if (allowed && path_count >= 1U) {
            allowed = true;
        } else {
            allowed = false;
        }

        if (!allowed) {
            bool is_write_attributes = false;

            if (coap_header_get_code(packet) == COAP_METHOD_PUT) {
                struct coap_option short_path_query_options[SIRENA_LWM2M_COAP_OPTIONS];
                int query_found = coap_find_options(packet, COAP_OPTION_URI_QUERY,
                                                   short_path_query_options,
                                                   ARRAY_SIZE(short_path_query_options));
                if (query_found > 0) {
                    is_write_attributes = true;
                }
            }

            if (!is_write_attributes) {
                (void)endpoint_send_message(response_type, response_message_id, token_length, token,
                                            COAP_RESPONSE_CODE_NOT_FOUND, false, 0U, -1, NULL, 0U,
                                            peer, peer_length);
                return;
            }
        }
    }

    for (size_t pad = path_count; pad < SIRENA_LWM2M_COAP_PATH_SEGMENTS; ++pad) {
        path[pad] = 0U;
    }

    observe = coap_get_option_int(packet, COAP_OPTION_OBSERVE);
    content_format = coap_get_option_int(packet, COAP_OPTION_CONTENT_FORMAT);
    payload = coap_packet_get_payload(packet, &payload_length);

    {
        struct coap_option query_options[SIRENA_LWM2M_COAP_OPTIONS];
        int found = coap_find_options(packet, COAP_OPTION_URI_QUERY, query_options,
                                     ARRAY_SIZE(query_options));
        if (found < 0) {
            found = 0;
        }
        if ((size_t)found > ARRAY_SIZE(query_options)) {
            found = (int)ARRAY_SIZE(query_options);
        }
        if (found > 0 && coap_header_get_code(packet) == COAP_METHOD_PUT) {
            write_attributes = true;
            size_t qi;

            for (qi = 0U; qi < (size_t)found; ++qi) {
                char key[8];
                char value[32];
                size_t key_length = 0U;
                size_t value_length = 0U;
                size_t k;
                const uint8_t *raw = query_options[qi].value;
                uint16_t len = query_options[qi].len;

                for (k = 0U; k < len && raw[k] != (uint8_t)'='; ++k) {
                    if (k < sizeof(key) - 1U) {
                        key[key_length++] = (char)raw[k];
                    }
                }
                key[key_length] = '\0';

                if (k < len) {
                    size_t v;

                    for (v = k + 1U; v < len; ++v) {
                        if (value_length < sizeof(value) - 1U) {
                            value[value_length++] = (char)raw[v];
                        }
                    }
                }
                value[value_length] = '\0';

                if (strcmp(key, "pmin") == 0 || strcmp(key, "pmax") == 0) {
                    char *endptr = NULL;
                    bool is_pmin = strcmp(key, "pmin") == 0;

                    if (value_length == 0U || value[0] == '-') {
                        attr_rc = -EINVAL;
                    } else {
                        errno = 0;
                        unsigned long parsed = strtoul(value, &endptr, 10);

                        if (endptr == value || *endptr != '\0' || errno == ERANGE ||
                            parsed > UINT32_MAX) {
                            attr_rc = -EINVAL;
                        } else {
                            if (is_pmin) {
                                request_attributes.has_pmin = true;
                                request_attributes.pmin = (uint32_t)parsed;
                            } else {
                                request_attributes.has_pmax = true;
                                request_attributes.pmax = (uint32_t)parsed;
                            }
                        }
                    }
                } else if (strcmp(key, "gt") == 0 || strcmp(key, "lt") == 0 ||
                           strcmp(key, "st") == 0) {
                    char *endptr = NULL;
                    double parsed;

                    if (value_length == 0U) {
                        attr_rc = -EINVAL;
                    } else if (value_length >= 2U && value[0] == '0' &&
                               (value[1] == 'x' || value[1] == 'X')) {
                        attr_rc = -EINVAL;
                    } else {
                        errno = 0;
                        parsed = strtod(value, &endptr);

                        if (endptr == value || *endptr != '\0' || errno == ERANGE ||
                            !isfinite(parsed)) {
                            attr_rc = -EINVAL;
                        } else {
                            if (strcmp(key, "gt") == 0) {
                                request_attributes.has_gt = true;
                                request_attributes.gt = parsed;
                            } else if (strcmp(key, "lt") == 0) {
                                request_attributes.has_lt = true;
                                request_attributes.lt = parsed;
                            } else {
                                request_attributes.has_st = true;
                                request_attributes.st = parsed;
                            }
                        }
                    }
                } else {
                    unknown_query = true;
                }
            }
        }
    }

    if (coap_header_get_code(packet) == COAP_METHOD_FETCH) {
        uint16_t oids[CONFIG_SIRENA_LWM2M_CODEC_MAX_VALUES];
        uint16_t iids[CONFIG_SIRENA_LWM2M_CODEC_MAX_VALUES];
        uint16_t rids[CONFIG_SIRENA_LWM2M_CODEC_MAX_VALUES];
        size_t target_count;
        struct sirena_lwm2m_codec_value values[CONFIG_SIRENA_LWM2M_CODEC_MAX_VALUES];
        size_t value_count = 0U;
        size_t target_index;
        bool unknown_path = false;
        bool not_readable = false;
        bool resource_error = false;
        bool invalid_target = false;
        bool unresolved_target = false;
        int negotiated = -1;
        bool negotiated_found = false;

        if (rc != 0) {
            (void)endpoint_send_message(response_type, response_message_id, token_length, token,
                                        COAP_RESPONSE_CODE_NOT_ACCEPTABLE, false, 0U, -1, NULL, 0U,
                                        peer, peer_length);
            return;
        }

        if (content_format < 0 || (content_format != (int)SIRENA_LWM2M_CF_SENML_JSON &&
                                   content_format != (int)SIRENA_LWM2M_CF_SENML_CBOR)) {
            (void)endpoint_send_message(response_type, response_message_id, token_length, token,
                                        COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT, false, 0U,
                                        -1, NULL, 0U, peer, peer_length);
            return;
        }

        if (content_format == (int)SIRENA_LWM2M_CF_SENML_JSON) {
            target_count = composite_collect_json((const char *)payload, payload_length, oids, iids,
                                                  rids, CONFIG_SIRENA_LWM2M_CODEC_MAX_VALUES,
                                                  &invalid_target, &unresolved_target);
        } else {
            target_count = composite_collect_cbor((const uint8_t *)payload, payload_length, oids,
                                                  iids, rids, CONFIG_SIRENA_LWM2M_CODEC_MAX_VALUES,
                                                  &invalid_target, &unresolved_target);
        }

        if (invalid_target) {
            (void)endpoint_send_message(response_type, response_message_id, token_length, token,
                                        COAP_RESPONSE_CODE_BAD_REQUEST, false, 0U, -1, NULL, 0U,
                                        peer, peer_length);
            return;
        }

        if (unresolved_target) {
            (void)endpoint_send_message(response_type, response_message_id, token_length, token,
                                        COAP_RESPONSE_CODE_NOT_FOUND, false, 0U, -1, NULL, 0U,
                                        peer, peer_length);
            return;
        }

        if (target_count > CONFIG_SIRENA_LWM2M_CODEC_MAX_VALUES) {
            (void)endpoint_send_message(response_type, response_message_id, token_length, token,
                                        COAP_RESPONSE_CODE_BAD_REQUEST, false, 0U, -1, NULL, 0U,
                                        peer, peer_length);
            return;
        }

        if (target_count == 0U) {
            (void)endpoint_send_message(response_type, response_message_id, token_length, token,
                                        COAP_RESPONSE_CODE_BAD_REQUEST, false, 0U, -1, NULL, 0U,
                                        peer, peer_length);
            return;
        }

        memset(values, 0, sizeof(values));
        for (target_index = 0U; target_index < target_count; ++target_index) {
            struct sirena_lwm2m_resource *resource;
            size_t raw_length = CONFIG_SIRENA_LWM2M_PROTOCOL_VALUE_MAX;

            if (sirena_lwm2m_find_resource(oids[target_index], iids[target_index],
                                            rids[target_index], &resource) != 0) {
                unknown_path = true;
                continue;
            }

            if ((resource->operations & SIRENA_LWM2M_OP_READ) == 0U || resource->read == NULL) {
                not_readable = true;
                continue;
            }

            if (sirena_lwm2m_get(oids[target_index], iids[target_index], rids[target_index],
                                 composite_raw[target_index], &raw_length) != 0) {
                resource_error = true;
                continue;
            }

            if (sirena_lwm2m_protocol_value_from_resource(resource, composite_raw[target_index], raw_length,
                                            &values[value_count]) != 0) {
                resource_error = true;
                continue;
            }

            values[value_count].object_id = oids[target_index];
            values[value_count].instance_id = iids[target_index];
            values[value_count].resource_id = rids[target_index];
            values[value_count].id = rids[target_index];
            value_count++;
        }

        if (unknown_path) {
            (void)endpoint_send_message(response_type, response_message_id, token_length, token,
                                        COAP_RESPONSE_CODE_NOT_FOUND, false, 0U, -1, NULL, 0U,
                                        peer, peer_length);
            return;
        }

        if (not_readable) {
            (void)endpoint_send_message(response_type, response_message_id, token_length, token,
                                        COAP_RESPONSE_CODE_NOT_ALLOWED, false, 0U, -1, NULL, 0U,
                                        peer, peer_length);
            return;
        }

        if (resource_error) {
            (void)endpoint_send_message(response_type, response_message_id, token_length, token,
                                        COAP_RESPONSE_CODE_INTERNAL_ERROR, false, 0U, -1, NULL, 0U,
                                        peer, peer_length);
            return;
        }

        if (value_count == 0U) {
            (void)endpoint_send_message(response_type, response_message_id, token_length, token,
                                        COAP_RESPONSE_CODE_BAD_REQUEST, false, 0U, -1, NULL, 0U,
                                        peer, peer_length);
            return;
        }

        {
            uint16_t preferred = (uint16_t)CONFIG_SIRENA_LWM2M_PREFERRED_CONTENT_FORMAT;
            uint16_t selected = 0U;

            if (sirena_lwm2m_content_format_negotiate_multi(accepted, accepted_count, preferred,
                                                           values, value_count, &selected) == 0) {
                negotiated = (int)selected;
                negotiated_found = true;
            }
        }

        if (!negotiated_found) {
            (void)endpoint_send_message(response_type, response_message_id, token_length, token,
                                        COAP_RESPONSE_CODE_NOT_ACCEPTABLE, false, 0U, -1, NULL, 0U,
                                        peer, peer_length);
            return;
        }

        size_t length = sizeof(response_payload);
        if (sirena_lwm2m_codec_encode((uint16_t)negotiated, values, value_count, response_payload,
                                     &length) != 0) {
            (void)endpoint_send_message(response_type, response_message_id, token_length, token,
                                        COAP_RESPONSE_CODE_INTERNAL_ERROR, false, 0U, -1, NULL, 0U,
                                        peer, peer_length);
            return;
        }

        (void)endpoint_send_message(response_type, response_message_id, token_length, token,
                                    COAP_RESPONSE_CODE_CONTENT, true, (uint16_t)negotiated, -1,
                                    response_payload, length, peer, peer_length);
        return;
    }

    if (coap_header_get_code(packet) == COAP_METHOD_GET) {
        if (rc == 0 && accepted_count > 0U &&
            accepted[0] == COAP_CONTENT_FORMAT_APP_LINK_FORMAT) {
            protocol_request.operation = SIRENA_LWM2M_OPERATION_DISCOVER;
        }

        if (protocol_request.operation != SIRENA_LWM2M_OPERATION_DISCOVER) {
            protocol_request.operation = SIRENA_LWM2M_OPERATION_READ;
        }
    }

    switch (coap_header_get_code(packet)) {
    case COAP_METHOD_GET:
        if (protocol_request.operation != SIRENA_LWM2M_OPERATION_DISCOVER) {
            protocol_request.operation = SIRENA_LWM2M_OPERATION_READ;
        }
        break;
    case COAP_METHOD_PUT:
        if (write_attributes) {
            protocol_request.operation = SIRENA_LWM2M_OPERATION_WRITE_ATTRIBUTES;
        } else {
            protocol_request.operation = SIRENA_LWM2M_OPERATION_WRITE;
        }
        break;
    case COAP_METHOD_POST:
        protocol_request.operation = SIRENA_LWM2M_OPERATION_EXECUTE;
        break;
    case COAP_METHOD_IPATCH:
        protocol_request.operation = SIRENA_LWM2M_OPERATION_WRITE_COMPOSITE;
        break;
    default:
        (void)endpoint_send_message(response_type, response_message_id, token_length, token,
                                    COAP_RESPONSE_CODE_NOT_ALLOWED, false, 0U, -1, NULL, 0U,
                                    peer, peer_length);
        return;
    }

    if (protocol_request.operation == SIRENA_LWM2M_OPERATION_READ ||
        protocol_request.operation == SIRENA_LWM2M_OPERATION_DISCOVER ||
        protocol_request.operation == SIRENA_LWM2M_OPERATION_WRITE_COMPOSITE) {
        if (rc != 0) {
            (void)endpoint_send_message(response_type, response_message_id, token_length, token,
                                        COAP_RESPONSE_CODE_NOT_ACCEPTABLE, false, 0U, -1, NULL,
                                        0U, peer, peer_length);
            return;
        }
    }

    if (protocol_request.operation == SIRENA_LWM2M_OPERATION_READ && observe == 0) {
        observer = endpoint_observer_find(token, token_length, peer);
        if (observer == NULL) {
            observer = endpoint_observer_alloc();
        }

        if (observer == NULL) {
            (void)endpoint_send_message(response_type, response_message_id, token_length, token,
                                        COAP_RESPONSE_CODE_SERVICE_UNAVAILABLE, false, 0U, -1,
                                        NULL, 0U, peer, peer_length);
            return;
        }
    }

    protocol_request.object_id = path[0];
    protocol_request.instance_id = path[1];
    protocol_request.resource_id = path[2];
    protocol_request.path_count = path_count;
    protocol_request.attributes = request_attributes;
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

    if (protocol_request.operation == SIRENA_LWM2M_OPERATION_WRITE_ATTRIBUTES &&
        (attr_rc != 0 || unknown_query)) {
        (void)endpoint_send_message(response_type, response_message_id, token_length, token,
                                    COAP_RESPONSE_CODE_BAD_REQUEST, false, 0U, -1, NULL, 0U, peer,
                                    peer_length);
        return;
    }

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
        memset(&observer->peer, 0, sizeof(observer->peer));
        memcpy(&observer->peer, peer, net_family2size(peer->sa_family));
        observer->peer_length = peer_length;
    }

    if (protocol_request.operation == SIRENA_LWM2M_OPERATION_READ && observe == 1) {
        struct sirena_lwm2m_coap_observer *cancelled;

        cancelled = endpoint_observer_find(token, token_length, peer);
        if (cancelled != NULL) {
            cancelled->used = false;
        }
    }

    (void)endpoint_send_message(response_type, response_message_id, token_length, token, code,
                                protocol_response.status == SIRENA_LWM2M_STATUS_CONTENT ||
                                    protocol_response.payload_length > 0U,
                                protocol_response.content_format,
                                observer != NULL ? (int)observer->sequence : -1,
                                protocol_response.payload, protocol_response.payload_length, peer,
                                peer_length);
}

static int endpoint_dispatch(uint8_t *data, size_t length, const struct net_sockaddr *peer,
                             net_socklen_t peer_length)
{
    struct coap_packet packet;

    if (coap_packet_parse(&packet, data, (uint16_t)length, dispatch_options,
                          ARRAY_SIZE(dispatch_options)) < 0) {
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
                                       enum coap_method method, bool content_format_present,
                                       uint16_t content_format, const void *payload,
                                       size_t payload_length,
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

    if (content_format_present) {
        rc = coap_append_option_int(&exchange.request, COAP_OPTION_CONTENT_FORMAT,
                                    content_format);
        if (rc < 0) {
            return rc;
        }
    }

    if (payload != NULL && payload_length > 0U) {
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

        {
            struct sirena_lwm2m_resource_attributes notify_attrs;
            int64_t now = k_uptime_get();

            if (sirena_lwm2m_get_resource_attributes(object_id, instance_id, resource_id,
                                                     &notify_attrs) == 0 &&
                notify_attrs.has_pmin && observer->last_notify_ms != 0 &&
                (now - observer->last_notify_ms) < (int64_t)notify_attrs.pmin * 1000) {
                continue;
            }
        }

        rc = sirena_lwm2m_protocol_read(object_id, instance_id, resource_id,
                                        observer->content_format, notify_payload, &length);
        if (rc != 0) {
            continue;
        }

        observer->sequence++;

        if (endpoint_send_message(COAP_TYPE_CON, coap_next_id(), observer->token_length,
                                  observer->token, COAP_RESPONSE_CODE_CONTENT, true,
                                  observer->content_format,
                                  (int)observer->sequence, notify_payload, length,
                                  net_sad(&observer->peer), observer->peer_length) == 0) {
            observer->last_notify_ms = k_uptime_get();
            sent++;
        }
    }

    return sent;
}


void sirena_lwm2m_coap_endpoint_enforce_pmax(void)
{
    size_t i;
    bool handled[ARRAY_SIZE(observers)] = { false };

    for (i = 0U; i < ARRAY_SIZE(observers); ++i) {
        struct sirena_lwm2m_coap_observer *observer = &observers[i];
        struct sirena_lwm2m_resource_attributes attrs;
        int64_t now;
        size_t j;
        bool already = false;

        if (!observer->used) {
            continue;
        }

        if (sirena_lwm2m_get_resource_attributes(observer->object_id, observer->instance_id,
                                                 observer->resource_id, &attrs) != 0 ||
            !attrs.has_pmax || observer->last_notify_ms == 0) {
            continue;
        }

        now = k_uptime_get();
        if (now - observer->last_notify_ms < (int64_t)attrs.pmax * 1000) {
            continue;
        }

        for (j = 0U; j < i; ++j) {
            if (handled[j] && observers[j].used &&
                observers[j].object_id == observer->object_id &&
                observers[j].instance_id == observer->instance_id &&
                observers[j].resource_id == observer->resource_id) {
                already = true;
                break;
            }
        }
        if (already) {
            continue;
        }

        (void)sirena_lwm2m_coap_endpoint_notify(observer->object_id, observer->instance_id,
                                                observer->resource_id);
        handled[i] = true;
    }
}
