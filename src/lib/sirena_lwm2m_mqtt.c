#include "sirena_lwm2m_bindings.h"
#include "sirena_lwm2m_credentials.h"
#include "sirena_lwm2m_link_format.h"
#include "sirena_lwm2m_mqtt_payload.h"
#include "sirena_lwm2m_uri.h"
#include <sirena/lwm2m/engine.h>
#include <sirena/lwm2m/mqtt.h>
#include <sirena/lwm2m/objects.h>
#include <sirena/lwm2m/protocol.h>
#include <sirena/lwm2m/codec.h>
#include <sirena/lwm2m/transport.h>
#include <zephyr/kernel.h>
#include <sirena/lwm2m/object_registry.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>
#include <mbedtls/ssl_ciphersuites.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIRENA_LWM2M_SECURITY_OBJECT_ID 0U
#define SIRENA_LWM2M_SERVER_OBJECT_ID 1U
#define SIRENA_LWM2M_SECURITY_URI_RESOURCE 0U
#define SIRENA_LWM2M_SECURITY_MODE_RESOURCE 2U
#define SIRENA_LWM2M_SERVER_LIFETIME_RESOURCE 1U

#define SIRENA_LWM2M_MQTT_BINDING_MODE "M"
#define SIRENA_LWM2M_MQTT_TOPIC_ROOT "lwm2m/rd/"
#define SIRENA_LWM2M_MQTT_WAIT_INTERVAL_MS 5
#define SIRENA_LWM2M_MQTT_TOKEN_BASE 0x80000000U
#define SIRENA_LWM2M_MQTT_LINK_FORMAT_CONTENT_FORMAT 40U

#define SIRENA_LWM2M_MQTT_TOPIC_CAPACITY                                       \
    (CONFIG_SIRENA_LWM2M_MQTT_TOPIC_PREFIX_MAX + sizeof(SIRENA_LWM2M_MQTT_TOPIC_ROOT) + \
     CONFIG_SIRENA_LWM2M_ENDPOINT_NAME_MAX + 1U)

struct sirena_lwm2m_mqtt_observation {
    bool used;
    uint32_t token;
    uint16_t object_id;
    uint16_t instance_id;
    uint16_t resource_id;
    uint16_t content_format;
    int64_t last_notify_ms;
};

static atomic_t transport_started;
static int mqtt_notify(uint16_t object_id, uint16_t instance_id, uint16_t resource_id);
static int mqtt_send(const struct sirena_lwm2m_send_record *records, size_t count);
static struct mqtt_client client;
static uint8_t rx_buffer[CONFIG_SIRENA_LWM2M_MQTT_BUFFER_SIZE];
static uint8_t tx_buffer[CONFIG_SIRENA_LWM2M_MQTT_BUFFER_SIZE];
static char topic[MAX(CONFIG_SIRENA_LWM2M_MQTT_TOPIC_MAX + 1U,
                      SIRENA_LWM2M_MQTT_TOPIC_CAPACITY)];
static char client_id[CONFIG_SIRENA_LWM2M_MQTT_CLIENT_ID_MAX + 1U];
static char topic_prefix[CONFIG_SIRENA_LWM2M_MQTT_TOPIC_PREFIX_MAX + 1U];
static char registration_payload[CONFIG_SIRENA_LWM2M_REGISTRATION_PAYLOAD_MAX + 1U];
static struct net_sockaddr_storage broker_address;
static struct sirena_lwm2m_mqtt_observation observations[CONFIG_SIRENA_LWM2M_MAX_OBSERVATIONS];
static uint8_t message_buffer[CONFIG_SIRENA_LWM2M_MQTT_BUFFER_SIZE];
static uint8_t value_buffer[CONFIG_SIRENA_LWM2M_PROTOCOL_PAYLOAD_MAX];
static uint32_t next_token = SIRENA_LWM2M_MQTT_TOKEN_BASE;
static bool connack_received;
static uint8_t connack_code;
static bool response_received;
static uint32_t pending_token;
static uint32_t response_result;
static sec_tag_t tls_sec_tags[] = { CONFIG_SIRENA_LWM2M_MQTT_SEC_TAG };
static const int psk_tls_ciphersuites[] = { MBEDTLS_TLS_PSK_WITH_AES_128_CCM_8 };
static const int cert_tls_ciphersuites[] = { MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8 };

static int mqtt_publish_payload(const void *data, size_t length);
static void mqtt_handle_message(const struct sirena_lwm2m_mqtt_message *message);

static int mqtt_transport_scheme(enum sirena_lwm2m_uri_scheme *scheme)
{
    char uri[CONFIG_SIRENA_LWM2M_SERVER_URI_MAX + 1U];
    size_t length = sizeof(uri);
    int rc;

    rc = sirena_lwm2m_get_string(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                 SIRENA_LWM2M_SECURITY_URI_RESOURCE, uri, &length);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_uri_scheme(uri, scheme);
    if (rc != 0) {
        return rc;
    }

    if (*scheme != SIRENA_LWM2M_URI_SCHEME_MQTT && *scheme != SIRENA_LWM2M_URI_SCHEME_MQTTS) {
        return -EINVAL;
    }

    return 0;
}

static int mqtt_security_mode_check(enum sirena_lwm2m_uri_scheme scheme)
{
    int64_t mode;
    int rc;

    rc = sirena_lwm2m_get_integer(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                  SIRENA_LWM2M_SECURITY_MODE_RESOURCE, &mode);
    if (rc != 0) {
        return rc;
    }

    switch (mode) {
    case SIRENA_LWM2M_SECURITY_MODE_PSK:
    case SIRENA_LWM2M_SECURITY_MODE_CERTIFICATE:
    case SIRENA_LWM2M_SECURITY_MODE_RPK:
        if (scheme != SIRENA_LWM2M_URI_SCHEME_MQTTS) {
            return -EINVAL;
        }

        return 0;
    case SIRENA_LWM2M_SECURITY_MODE_NOSEC:
        if (scheme != SIRENA_LWM2M_URI_SCHEME_MQTT) {
            return -EINVAL;
        }

        if (!IS_ENABLED(CONFIG_SIRENA_LWM2M_TRANSPORT_NOSEC)) {
            return -EPERM;
        }

        return 0;
    default:
        return -ENOTSUP;
    }
}

static int mqtt_transport_setup(enum sirena_lwm2m_uri_scheme scheme)
{
    int rc;

    if (scheme == SIRENA_LWM2M_URI_SCHEME_MQTT) {
        client.transport.type = MQTT_TRANSPORT_NON_SECURE;

        return 0;
    }

    if (scheme != SIRENA_LWM2M_URI_SCHEME_MQTTS) {
        return -EINVAL;
    }

    {
        int64_t mode;

        rc = sirena_lwm2m_get_integer(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                      SIRENA_LWM2M_SECURITY_MODE_RESOURCE,
                                      &mode);
        if (rc != 0) {
            return rc;
        }

        rc = sirena_lwm2m_credentials_register_mode(CONFIG_SIRENA_LWM2M_MQTT_SEC_TAG,
                                                   mode);
        if (rc != 0) {
            return rc;
        }

        if (mode == SIRENA_LWM2M_SECURITY_MODE_CERTIFICATE ||
            mode == SIRENA_LWM2M_SECURITY_MODE_RPK) {
            client.transport.tls.config.peer_verify = ZSOCK_TLS_PEER_VERIFY_REQUIRED;
            client.transport.tls.config.cipher_list = cert_tls_ciphersuites;
            client.transport.tls.config.cipher_count = ARRAY_SIZE(cert_tls_ciphersuites);
        } else {
            client.transport.tls.config.peer_verify = ZSOCK_TLS_PEER_VERIFY_NONE;
            client.transport.tls.config.cipher_list = psk_tls_ciphersuites;
            client.transport.tls.config.cipher_count = ARRAY_SIZE(psk_tls_ciphersuites);
        }
    }

    client.transport.type = MQTT_TRANSPORT_SECURE;
    client.transport.tls.config.sec_tag_list = tls_sec_tags;
    client.transport.tls.config.sec_tag_count = ARRAY_SIZE(tls_sec_tags);

    return 0;
}

static int mqtt_build_topic(void)
{
    const char *endpoint_name = sirena_lwm2m_transport_endpoint_name();
    int written;

    if (endpoint_name[0] == '\0') {
        return -EINVAL;
    }

    if (topic_prefix[0] != '\0') {
        written = snprintf(topic, sizeof(topic), "%s/%s%s", topic_prefix,
                           SIRENA_LWM2M_MQTT_TOPIC_ROOT, endpoint_name);
    } else {
        written = snprintf(topic, sizeof(topic), "%s%s", SIRENA_LWM2M_MQTT_TOPIC_ROOT,
                           endpoint_name);
    }

    if (written < 0 || (size_t)written >= sizeof(topic)) {
        return -ENOMEM;
    }

    return 0;
}

static int mqtt_build_client_id(void)
{
    const char *endpoint_name = sirena_lwm2m_transport_endpoint_name();
    size_t length;

    if (client_id[0] != '\0') {
        return 0;
    }

    if (endpoint_name[0] == '\0') {
        return -EINVAL;
    }

    length = strlen(endpoint_name);
    if (length > CONFIG_SIRENA_LWM2M_MQTT_CLIENT_ID_MAX) {
        length = CONFIG_SIRENA_LWM2M_MQTT_CLIENT_ID_MAX;
    }

    memcpy(client_id, endpoint_name, length);
    client_id[length] = '\0';

    return 0;
}

static int mqtt_publish_payload(const void *data, size_t length)
{
    struct mqtt_publish_param param = {
        .message = {
            .topic = {
                .topic = {
                    .utf8 = (const uint8_t *)topic,
                    .size = (uint32_t)strlen(topic),
                },
                .qos = MQTT_QOS_0_AT_MOST_ONCE,
            },
            .payload = {
                .data = (uint8_t *)data,
                .len = (uint32_t)length,
            },
        },
        .message_id = 0U,
        .dup_flag = 0U,
        .retain_flag = 0U,
    };

    return mqtt_publish(&client, &param);
}

static int mqtt_set_text(char *target, size_t capacity, const char *source)
{
    size_t length = strlen(source);

    if (length >= capacity) {
        return -EINVAL;
    }

    memcpy(target, source, length + 1U);

    return 0;
}

static int mqtt_send_response(uint32_t token, uint32_t result, bool content_format_present,
                               uint16_t content_format, const uint8_t *payload,
                               size_t payload_length)
{
    struct sirena_lwm2m_mqtt_message message = {
        .has_operation = false,
        .token = token,
        .has_result = true,
        .result = result,
        .has_content_format = content_format_present,
        .content_format = content_format,
        .has_payload = payload != NULL && payload_length > 0U,
        .payload = payload,
        .payload_length = payload_length,
    };
    size_t length = sizeof(message_buffer);
    int rc;

    rc = sirena_lwm2m_mqtt_message_encode(&message, message_buffer, &length);
    if (rc != 0) {
        return rc;
    }

    return mqtt_publish_payload(message_buffer, length);
}

static uint32_t mqtt_result_from_status(enum sirena_lwm2m_status status)
{
    switch (status) {
    case SIRENA_LWM2M_STATUS_CHANGED:
        return SIRENA_LWM2M_MQTT_RESULT_CHANGED;
    case SIRENA_LWM2M_STATUS_CONTENT:
        return SIRENA_LWM2M_MQTT_RESULT_CONTENT;
    case SIRENA_LWM2M_STATUS_BAD_REQUEST:
        return SIRENA_LWM2M_MQTT_RESULT_BAD_REQUEST;
    case SIRENA_LWM2M_STATUS_NOT_FOUND:
        return SIRENA_LWM2M_MQTT_RESULT_NOT_FOUND;
    case SIRENA_LWM2M_STATUS_METHOD_NOT_ALLOWED:
        return SIRENA_LWM2M_MQTT_RESULT_NOT_ALLOWED;
    case SIRENA_LWM2M_STATUS_NOT_ACCEPTABLE:
        return SIRENA_LWM2M_MQTT_RESULT_NOT_ACCEPTABLE;
    case SIRENA_LWM2M_STATUS_UNSUPPORTED_CONTENT_FORMAT:
        return SIRENA_LWM2M_MQTT_RESULT_UNSUPPORTED_FORMAT;
    default:
        return SIRENA_LWM2M_MQTT_RESULT_INTERNAL_ERROR;
    }
}

static int mqtt_parse_attribute_pair(const char *key, size_t key_length, const char *value,
                                     size_t value_length,
                                     struct sirena_lwm2m_resource_attributes *attributes,
                                     bool *unknown)
{
    char key_buffer[8];
    char value_buffer[32];

    if (key_length >= sizeof(key_buffer)) {
        *unknown = true;
        return 0;
    }
    memcpy(key_buffer, key, key_length);
    key_buffer[key_length] = '\0';

    if (value_length >= sizeof(value_buffer)) {
        return -EINVAL;
    }
    memcpy(value_buffer, value, value_length);
    value_buffer[value_length] = '\0';

    if (strcmp(key_buffer, "pmin") == 0 || strcmp(key_buffer, "pmax") == 0) {
        char *endptr = NULL;
        bool is_pmin = strcmp(key_buffer, "pmin") == 0;

        if (value_length == 0U || value_buffer[0] == '-') {
            return -EINVAL;
        }

        errno = 0;
        unsigned long parsed = strtoul(value_buffer, &endptr, 10);

        if (endptr == value_buffer || *endptr != '\0' || errno == ERANGE ||
            parsed > UINT32_MAX) {
            return -EINVAL;
        }

        if (is_pmin) {
            attributes->has_pmin = true;
            attributes->pmin = (uint32_t)parsed;
        } else {
            attributes->has_pmax = true;
            attributes->pmax = (uint32_t)parsed;
        }
    } else if (strcmp(key_buffer, "gt") == 0 || strcmp(key_buffer, "lt") == 0 ||
               strcmp(key_buffer, "st") == 0) {
        char *endptr = NULL;
        double parsed;

        if (value_length == 0U) {
            return -EINVAL;
        }

        if (value_length >= 2U && value_buffer[0] == '0' &&
            (value_buffer[1] == 'x' || value_buffer[1] == 'X')) {
            return -EINVAL;
        }

        errno = 0;
        parsed = strtod(value_buffer, &endptr);

        if (endptr == value_buffer || *endptr != '\0' || errno == ERANGE ||
            !isfinite(parsed)) {
            return -EINVAL;
        }

        if (strcmp(key_buffer, "gt") == 0) {
            attributes->has_gt = true;
            attributes->gt = parsed;
        } else if (strcmp(key_buffer, "lt") == 0) {
            attributes->has_lt = true;
            attributes->lt = parsed;
        } else {
            attributes->has_st = true;
            attributes->st = parsed;
        }
    } else {
        *unknown = true;
    }

    return 0;
}

static int mqtt_parse_uri(const char *uri, uint16_t *object_id, uint16_t *instance_id,
                           uint16_t *resource_id, size_t *path_count,
                           struct sirena_lwm2m_resource_attributes *attributes)
{
    uint16_t values[3] = {0U, 0U, 0U};
    size_t count = 0U;
    const char *cursor;
    bool unknown = false;
    int attr_rc = 0;

    if (uri == NULL || uri[0] != '/') {
        return -EINVAL;
    }

    if (attributes != NULL) {
        memset(attributes, 0, sizeof(*attributes));
    }

    cursor = uri + 1;

    while (count < ARRAY_SIZE(values) && *cursor >= '0' && *cursor <= '9') {
        uint32_t value = 0U;

        while (*cursor >= '0' && *cursor <= '9') {
            value = value * 10U + (uint32_t)(*cursor - '0');
            if (value > UINT16_MAX) {
                return -EINVAL;
            }

            cursor++;
        }

        values[count++] = (uint16_t)value;

        if (*cursor == '\0' || *cursor == '?') {
            break;
        }

        if (*cursor != '/') {
            return -EINVAL;
        }

        cursor++;

        if (*cursor == '\0' || *cursor == '?') {
            return -EINVAL;
        }
    }

    if (count == 0U) {
        return -EINVAL;
    }

    if (*cursor != '\0' && *cursor != '?') {
        return -EINVAL;
    }

    *object_id = values[0];
    *instance_id = (count > 1U) ? values[1] : 0U;
    *resource_id = (count > 2U) ? values[2] : 0U;

    if (path_count != NULL) {
        *path_count = count;
    }

    if (*cursor == '?') {
        cursor++;
        while (*cursor != '\0') {
            const char *key = cursor;
            const char *eq;
            const char *end;

            eq = strchr(cursor, '=');
            end = strchr(cursor, '&');
            if (end == NULL) {
                end = cursor + strlen(cursor);
            }

            if (eq == NULL || eq >= end) {
                unknown = true;
                cursor = end;
                if (*cursor == '&') {
                    cursor++;
                }
                continue;
            }

            if (attributes == NULL) {
                return -EINVAL;
            }

            attr_rc = mqtt_parse_attribute_pair(key, (size_t)(eq - key), eq + 1,
                                               (size_t)(end - eq - 1), attributes, &unknown);
            if (attr_rc != 0) {
                return attr_rc;
            }

            cursor = end;
            if (*cursor == '&') {
                cursor++;
            }
        }
    }

    if (unknown) {
        return -EINVAL;
    }

    return 0;
}

static struct sirena_lwm2m_mqtt_observation *mqtt_observation_find(uint32_t token)
{
    size_t i;

    for (i = 0U; i < ARRAY_SIZE(observations); ++i) {
        if (observations[i].used && observations[i].token == token) {
            return &observations[i];
        }
    }

    return NULL;
}

static struct sirena_lwm2m_mqtt_observation *mqtt_observation_alloc(void)
{
    size_t i;

    for (i = 0U; i < ARRAY_SIZE(observations); ++i) {
        if (!observations[i].used) {
            return &observations[i];
        }
    }

    return NULL;
}

static int mqtt_read_resource(const struct sirena_lwm2m_mqtt_message *message,
                              struct sirena_lwm2m_protocol_response *response)
{
    struct sirena_lwm2m_protocol_request request = {
        .operation = SIRENA_LWM2M_OPERATION_READ,
        .payload = NULL,
        .payload_length = 0U,
        .content_format_present = false,
        .content_format = 0U,
        .accepted_formats = NULL,
        .accepted_format_count = 0U,
    };
    int rc;

    rc = mqtt_parse_uri(message->uri, &request.object_id, &request.instance_id,
                        &request.resource_id, &request.path_count, &request.attributes);
    if (rc != 0) {
        return rc;
    }

    response->payload = value_buffer;
    response->payload_capacity = sizeof(value_buffer);
    response->payload_length = 0U;
    response->content_format = 0U;
    response->status = SIRENA_LWM2M_STATUS_INTERNAL_ERROR;

    (void)sirena_lwm2m_protocol_execute(&request, response);

    return 0;
}

static void mqtt_handle_read(const struct sirena_lwm2m_mqtt_message *message)
{
    struct sirena_lwm2m_protocol_response response;
    int rc;

    rc = mqtt_read_resource(message, &response);
    if (rc != 0) {
        (void)mqtt_send_response(message->token, SIRENA_LWM2M_MQTT_RESULT_BAD_REQUEST, false, 0U,
                                  NULL, 0U);
        return;
    }

    (void)mqtt_send_response(message->token, mqtt_result_from_status(response.status),
                              response.status == SIRENA_LWM2M_STATUS_CONTENT,
                              response.content_format, response.payload, response.payload_length);
}

static void mqtt_handle_write(const struct sirena_lwm2m_mqtt_message *message)
{
    bool write_attributes = message->has_uri && message->uri[0] != '\0' &&
                            strchr(message->uri, '?') != NULL;
    struct sirena_lwm2m_protocol_response response = {
        .status = SIRENA_LWM2M_STATUS_INTERNAL_ERROR,
        .content_format = 0U,
        .payload = NULL,
        .payload_capacity = 0U,
        .payload_length = 0U,
    };
    struct sirena_lwm2m_protocol_request request = {
        .operation = SIRENA_LWM2M_OPERATION_WRITE,
        .payload = message->payload,
        .payload_length = message->payload_length,
        .content_format_present = message->has_content_format,
        .content_format = (uint16_t)message->content_format,
        .accepted_formats = NULL,
        .accepted_format_count = 0U,
    };
    int rc;

    if (write_attributes) {
        request.operation = SIRENA_LWM2M_OPERATION_WRITE_ATTRIBUTES;
    }

    rc = mqtt_parse_uri(message->uri, &request.object_id, &request.instance_id,
                        &request.resource_id, &request.path_count, &request.attributes);
    if (rc != 0) {
        (void)mqtt_send_response(message->token, SIRENA_LWM2M_MQTT_RESULT_BAD_REQUEST, false, 0U,
                                  NULL, 0U);
        return;
    }

    (void)sirena_lwm2m_protocol_execute(&request, &response);

    (void)mqtt_send_response(message->token, mqtt_result_from_status(response.status), false, 0U,
                              NULL, 0U);
}

static void mqtt_handle_write_attributes(const struct sirena_lwm2m_mqtt_message *message)
{
    struct sirena_lwm2m_protocol_response response = {
        .status = SIRENA_LWM2M_STATUS_INTERNAL_ERROR,
        .content_format = 0U,
        .payload = NULL,
        .payload_capacity = 0U,
        .payload_length = 0U,
    };
    struct sirena_lwm2m_protocol_request request = {
        .operation = SIRENA_LWM2M_OPERATION_WRITE_ATTRIBUTES,
        .payload = NULL,
        .payload_length = 0U,
        .content_format_present = false,
        .content_format = 0U,
        .accepted_formats = NULL,
        .accepted_format_count = 0U,
    };
    int rc;

    rc = mqtt_parse_uri(message->uri, &request.object_id, &request.instance_id,
                        &request.resource_id, &request.path_count, &request.attributes);
    if (rc != 0) {
        (void)mqtt_send_response(message->token, SIRENA_LWM2M_MQTT_RESULT_BAD_REQUEST, false, 0U,
                                  NULL, 0U);
        return;
    }

    (void)sirena_lwm2m_protocol_execute(&request, &response);

    (void)mqtt_send_response(message->token, mqtt_result_from_status(response.status), false, 0U,
                              NULL, 0U);
}

static int mqtt_discover_resource(const struct sirena_lwm2m_mqtt_message *message,
                                   struct sirena_lwm2m_protocol_response *response)
{
    struct sirena_lwm2m_protocol_request request = {
        .operation = SIRENA_LWM2M_OPERATION_DISCOVER,
        .payload = NULL,
        .payload_length = 0U,
        .content_format_present = false,
        .content_format = 0U,
        .accepted_formats = NULL,
        .accepted_format_count = 0U,
    };
    int rc;

    rc = mqtt_parse_uri(message->uri, &request.object_id, &request.instance_id,
                        &request.resource_id, &request.path_count, &request.attributes);
    if (rc != 0) {
        return rc;
    }

    response->payload = value_buffer;
    response->payload_capacity = sizeof(value_buffer);
    response->payload_length = 0U;
    response->content_format = 0U;
    response->status = SIRENA_LWM2M_STATUS_INTERNAL_ERROR;

    (void)sirena_lwm2m_protocol_execute(&request, response);

    return 0;
}

static void mqtt_handle_discover(const struct sirena_lwm2m_mqtt_message *message)
{
    struct sirena_lwm2m_protocol_response response;
    int rc;

    rc = mqtt_discover_resource(message, &response);
    if (rc != 0) {
        (void)mqtt_send_response(message->token, SIRENA_LWM2M_MQTT_RESULT_BAD_REQUEST, false, 0U,
                                  NULL, 0U);
        return;
    }

    (void)mqtt_send_response(message->token, mqtt_result_from_status(response.status),
                              response.status == SIRENA_LWM2M_STATUS_CONTENT,
                              response.content_format, response.payload, response.payload_length);
}

static void mqtt_handle_execute(const struct sirena_lwm2m_mqtt_message *message)
{
    struct sirena_lwm2m_protocol_response response = {
        .status = SIRENA_LWM2M_STATUS_INTERNAL_ERROR,
        .content_format = 0U,
        .payload = NULL,
        .payload_capacity = 0U,
        .payload_length = 0U,
    };
    struct sirena_lwm2m_protocol_request request = {
        .operation = SIRENA_LWM2M_OPERATION_EXECUTE,
        .payload = message->payload,
        .payload_length = message->payload_length,
        .content_format_present = false,
        .content_format = 0U,
        .accepted_formats = NULL,
        .accepted_format_count = 0U,
    };
    int rc;

    rc = mqtt_parse_uri(message->uri, &request.object_id, &request.instance_id,
                        &request.resource_id, &request.path_count, &request.attributes);
    if (rc != 0) {
        (void)mqtt_send_response(message->token, SIRENA_LWM2M_MQTT_RESULT_BAD_REQUEST, false, 0U,
                                  NULL, 0U);
        return;
    }

    (void)sirena_lwm2m_protocol_execute(&request, &response);

    (void)mqtt_send_response(message->token, mqtt_result_from_status(response.status), false, 0U,
                              NULL, 0U);
}

static void mqtt_handle_observe(const struct sirena_lwm2m_mqtt_message *message)
{
    struct sirena_lwm2m_protocol_response response;
    struct sirena_lwm2m_mqtt_observation *observation;
    int rc;

    rc = mqtt_read_resource(message, &response);
    if (rc != 0) {
        (void)mqtt_send_response(message->token, SIRENA_LWM2M_MQTT_RESULT_BAD_REQUEST, false, 0U,
                                  NULL, 0U);
        return;
    }

    if (response.status != SIRENA_LWM2M_STATUS_CONTENT) {
        (void)mqtt_send_response(message->token, mqtt_result_from_status(response.status), false,
                                  0U, NULL, 0U);
        return;
    }

    observation = mqtt_observation_find(message->token);
    if (observation == NULL) {
        observation = mqtt_observation_alloc();
    }

    size_t observed_path_count = 0U;
    struct sirena_lwm2m_resource_attributes ignored_attributes;

    if (observation == NULL) {
        (void)mqtt_send_response(message->token, SIRENA_LWM2M_MQTT_RESULT_UNAVAILABLE, false, 0U,
                                  NULL, 0U);
        return;
    }

    rc = mqtt_parse_uri(message->uri, &observation->object_id, &observation->instance_id,
                        &observation->resource_id, &observed_path_count, &ignored_attributes);
    if (rc != 0) {
        (void)mqtt_send_response(message->token, SIRENA_LWM2M_MQTT_RESULT_BAD_REQUEST, false, 0U,
                                  NULL, 0U);
        return;
    }

    observation->used = true;
    observation->token = message->token;
    observation->content_format = response.content_format;
    observation->last_notify_ms = k_uptime_get();

    (void)mqtt_send_response(message->token, SIRENA_LWM2M_MQTT_RESULT_CONTENT, false, 0U, NULL,
                              0U);
}

static void mqtt_handle_cancel_observe(const struct sirena_lwm2m_mqtt_message *message)
{
    struct sirena_lwm2m_mqtt_observation *observation;

    observation = mqtt_observation_find(message->token);
    if (observation != NULL) {
        observation->used = false;
    }

    (void)mqtt_send_response(message->token, SIRENA_LWM2M_MQTT_RESULT_CONTENT, false, 0U, NULL, 0U);
}

static void mqtt_handle_read_composite(const struct sirena_lwm2m_mqtt_message *message)
{
    struct sirena_lwm2m_protocol_request request = {
        .operation = SIRENA_LWM2M_OPERATION_READ_COMPOSITE,
        .payload = message->payload,
        .payload_length = message->payload_length,
        .content_format_present = message->has_content_format,
        .content_format = (uint16_t)message->content_format,
        .accepted_formats = NULL,
        .accepted_format_count = 0U,
    };
    struct sirena_lwm2m_protocol_response response = {
        .status = SIRENA_LWM2M_STATUS_INTERNAL_ERROR,
        .content_format = 0U,
        .payload = value_buffer,
        .payload_capacity = sizeof(value_buffer),
        .payload_length = 0U,
    };
    int rc;

    rc = mqtt_parse_uri(message->uri, &request.object_id, &request.instance_id,
                        &request.resource_id, &request.path_count, &request.attributes);
    if (rc != 0) {
        (void)mqtt_send_response(message->token, SIRENA_LWM2M_MQTT_RESULT_BAD_REQUEST, false, 0U,
                                  NULL, 0U);
        return;
    }

    (void)sirena_lwm2m_protocol_execute(&request, &response);

    (void)mqtt_send_response(message->token, mqtt_result_from_status(response.status),
                              response.payload_length > 0U, response.content_format,
                              response.payload, response.payload_length);
}

static void mqtt_handle_write_composite(const struct sirena_lwm2m_mqtt_message *message)
{
    struct sirena_lwm2m_protocol_request request = {
        .operation = SIRENA_LWM2M_OPERATION_WRITE_COMPOSITE,
        .payload = message->payload,
        .payload_length = message->payload_length,
        .content_format_present = message->has_content_format,
        .content_format = (uint16_t)message->content_format,
        .accepted_formats = NULL,
        .accepted_format_count = 0U,
    };
    struct sirena_lwm2m_protocol_response response = {
        .status = SIRENA_LWM2M_STATUS_INTERNAL_ERROR,
        .content_format = 0U,
        .payload = value_buffer,
        .payload_capacity = sizeof(value_buffer),
        .payload_length = 0U,
    };
    int rc;

    rc = mqtt_parse_uri(message->uri, &request.object_id, &request.instance_id,
                        &request.resource_id, &request.path_count, &request.attributes);
    if (rc != 0) {
        (void)mqtt_send_response(message->token, SIRENA_LWM2M_MQTT_RESULT_BAD_REQUEST, false, 0U,
                                  NULL, 0U);
        return;
    }

    (void)sirena_lwm2m_protocol_execute(&request, &response);

    (void)mqtt_send_response(message->token, mqtt_result_from_status(response.status),
                              response.payload_length > 0U, response.content_format,
                              response.payload, response.payload_length);
}

static void mqtt_handle_message(const struct sirena_lwm2m_mqtt_message *message)
{
    if (message->has_result) {
        if (pending_token != 0U && message->token == pending_token) {
            response_received = true;
            response_result = message->result;
        }

        return;
    }

    if (!message->has_operation) {
        return;
    }

    switch (message->operation) {
    case SIRENA_LWM2M_MQTT_OPERATION_REGISTER:
    case SIRENA_LWM2M_MQTT_OPERATION_UPDATE:
    case SIRENA_LWM2M_MQTT_OPERATION_DEREGISTER:
    case SIRENA_LWM2M_MQTT_OPERATION_NOTIFY:
    case SIRENA_LWM2M_MQTT_OPERATION_SEND:
        return;
    case SIRENA_LWM2M_MQTT_OPERATION_READ:
        mqtt_handle_read(message);
        break;
    case SIRENA_LWM2M_MQTT_OPERATION_WRITE:
        mqtt_handle_write(message);
        break;
    case SIRENA_LWM2M_MQTT_OPERATION_READ_COMPOSITE:
        mqtt_handle_read_composite(message);
        break;
    case SIRENA_LWM2M_MQTT_OPERATION_WRITE_COMPOSITE:
        mqtt_handle_write_composite(message);
        break;
    case SIRENA_LWM2M_MQTT_OPERATION_WRITE_ATTRIBUTES:
        mqtt_handle_write_attributes(message);
        break;
    case SIRENA_LWM2M_MQTT_OPERATION_DISCOVER:
        mqtt_handle_discover(message);
        break;
    case SIRENA_LWM2M_MQTT_OPERATION_EXECUTE:
        mqtt_handle_execute(message);
        break;
    case SIRENA_LWM2M_MQTT_OPERATION_OBSERVE:
        mqtt_handle_observe(message);
        break;
    case SIRENA_LWM2M_MQTT_OPERATION_CANCEL_OBSERVE:
        mqtt_handle_cancel_observe(message);
        break;
    default:
        (void)mqtt_send_response(message->token, SIRENA_LWM2M_MQTT_RESULT_NOT_IMPLEMENTED, false,
                                  0U, NULL, 0U);
        break;
    }
}

static int mqtt_discard_payload(struct mqtt_client *mqtt, size_t length)
{
    uint8_t scratch[32];
    size_t received = 0U;

    while (received < length) {
        size_t chunk = MIN(sizeof(scratch), length - received);
        int rc = mqtt_read_publish_payload_blocking(mqtt, scratch, chunk);

        if (rc <= 0) {
            return rc;
        }

        received += (size_t)rc;
    }

    return 0;
}

static int mqtt_read_payload(struct mqtt_client *mqtt, uint8_t *buffer, size_t capacity,
                             size_t length, size_t *stored)
{
    size_t received = 0U;

    if (length > capacity) {
        int rc = mqtt_discard_payload(mqtt, length);

        if (rc != 0) {
            return rc;
        }

        return -EMSGSIZE;
    }

    while (received < length) {
        size_t chunk = length - received;
        int rc;

        rc = mqtt_read_publish_payload_blocking(mqtt, buffer + *stored, chunk);
        if (rc <= 0) {
            return rc;
        }

        received += (size_t)rc;
        *stored += (size_t)rc;
    }

    return 0;
}

static void mqtt_event_handler(struct mqtt_client *mqtt, const struct mqtt_evt *event)
{
    struct sirena_lwm2m_mqtt_message message;

    (void)mqtt;

    switch (event->type) {
    case MQTT_EVT_CONNACK:
        connack_received = true;
        connack_code = event->param.connack.return_code;
        break;
    case MQTT_EVT_PUBLISH: {
        const struct mqtt_utf8 *incoming = &event->param.publish.message.topic.topic;
        size_t length = 0U;
        int rc;

        rc = mqtt_read_payload(&client, message_buffer, sizeof(message_buffer),
                               event->param.publish.message.payload.len, &length);
        if (rc != 0) {
            return;
        }

        if (incoming->size != strlen(topic) ||
            memcmp(incoming->utf8, topic, incoming->size) != 0) {
            return;
        }

        if (sirena_lwm2m_mqtt_message_decode(message_buffer, length, &message) != 0) {
            return;
        }

        mqtt_handle_message(&message);
        break;
    }
    default:
        break;
    }
}

static int mqtt_wait_connack(void)
{
    int64_t deadline = k_uptime_get() + CONFIG_SIRENA_LWM2M_MQTT_RESPONSE_TIMEOUT_MS;

    while (!connack_received) {
        int64_t now = k_uptime_get();

        if (now >= deadline) {
            return -ETIMEDOUT;
        }

        if (mqtt_input(&client) < 0) {
            return -EIO;
        }

        if (!connack_received) {
            k_msleep(SIRENA_LWM2M_MQTT_WAIT_INTERVAL_MS);
        }
    }

    if (connack_code != MQTT_CONNECTION_ACCEPTED) {
        return -ECONNREFUSED;
    }

    return 0;
}

static int mqtt_wait_response(uint32_t token, uint32_t *result)
{
    int64_t deadline = k_uptime_get() + CONFIG_SIRENA_LWM2M_MQTT_RESPONSE_TIMEOUT_MS;

    while (true) {
        int64_t now = k_uptime_get();

        if (response_received) {
            response_received = false;
            pending_token = 0U;
            *result = response_result;

            return 0;
        }

        if (now >= deadline) {
            pending_token = 0U;

            return -ETIMEDOUT;
        }

        if (mqtt_input(&client) < 0) {
            pending_token = 0U;

            return -EIO;
        }

        k_msleep(SIRENA_LWM2M_MQTT_WAIT_INTERVAL_MS);
    }
}

static int mqtt_result_to_error(uint32_t result)
{
    switch (result) {
    case SIRENA_LWM2M_MQTT_RESULT_REGISTERED:
    case SIRENA_LWM2M_MQTT_RESULT_DEREGISTERED:
    case SIRENA_LWM2M_MQTT_RESULT_CHANGED:
    case SIRENA_LWM2M_MQTT_RESULT_CONTENT:
        return 0;
    case SIRENA_LWM2M_MQTT_RESULT_BAD_REQUEST:
        return -EBADMSG;
    case SIRENA_LWM2M_MQTT_RESULT_FORBIDDEN:
        return -EACCES;
    case SIRENA_LWM2M_MQTT_RESULT_NOT_FOUND:
        return -ENOENT;
    case SIRENA_LWM2M_MQTT_RESULT_CONFLICT:
        return -EEXIST;
    case SIRENA_LWM2M_MQTT_RESULT_PRECONDITION_FAILED:
        return -EPROTO;
    default:
        return -EIO;
    }
}

static uint32_t mqtt_next_token(void)
{
    uint32_t token = next_token;

    next_token++;

    return token;
}

static int mqtt_request(struct sirena_lwm2m_mqtt_message *message, uint32_t expected_result,
                         uint32_t *result)
{
    size_t length = sizeof(message_buffer);
    uint32_t token = mqtt_next_token();
    int rc;

    message->token = token;
    pending_token = token;

    rc = sirena_lwm2m_mqtt_message_encode(message, message_buffer, &length);
    if (rc != 0) {
        pending_token = 0U;

        return rc;
    }

    rc = mqtt_publish_payload(message_buffer, length);
    if (rc != 0) {
        pending_token = 0U;

        return rc;
    }

    rc = mqtt_wait_response(token, result);
    if (rc != 0) {
        return rc;
    }

    if (*result != expected_result) {
        return mqtt_result_to_error(*result);
    }

    return 0;
}

static int mqtt_register(void)
{
    struct sirena_lwm2m_mqtt_message message = {
        .has_operation = true,
        .operation = SIRENA_LWM2M_MQTT_OPERATION_REGISTER,
        .token = 0U,
        .has_endpoint = true,
        .has_lifetime = true,
        .has_version = true,
        .has_binding = true,
        .has_content_format = true,
        .content_format = SIRENA_LWM2M_MQTT_LINK_FORMAT_CONTENT_FORMAT,
        .has_payload = true,
    };
    const char *endpoint_name = sirena_lwm2m_transport_endpoint_name();
    size_t link_length = 0U;
    int64_t lifetime;
    uint32_t result = 0U;
    int rc;

    if (endpoint_name[0] == '\0') {
        return -EINVAL;
    }

    rc = sirena_lwm2m_get_integer(SIRENA_LWM2M_SERVER_OBJECT_ID, 0U,
                                  SIRENA_LWM2M_SERVER_LIFETIME_RESOURCE, &lifetime);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_link_format_objects(registration_payload, sizeof(registration_payload),
                                          &link_length);
    if (rc != 0) {
        return rc;
    }

    if (lifetime < 0) {
        return -EINVAL;
    }

    message.lifetime = (uint32_t)lifetime;

    rc = mqtt_set_text(message.endpoint, sizeof(message.endpoint), endpoint_name);
    if (rc != 0) {
        return rc;
    }

    rc = mqtt_set_text(message.version, sizeof(message.version),
                        CONFIG_SIRENA_LWM2M_LWM2M_VERSION);
    if (rc != 0) {
        return rc;
    }

    rc = mqtt_set_text(message.binding, sizeof(message.binding), SIRENA_LWM2M_MQTT_BINDING_MODE);
    if (rc != 0) {
        return rc;
    }

    message.payload = (const uint8_t *)registration_payload;
    message.payload_length = link_length;

    return mqtt_request(&message, SIRENA_LWM2M_MQTT_RESULT_REGISTERED, &result);
}

static int mqtt_update(void)
{
    struct sirena_lwm2m_mqtt_message message = {
        .has_operation = true,
        .operation = SIRENA_LWM2M_MQTT_OPERATION_UPDATE,
        .token = 0U,
        .has_lifetime = true,
        .has_binding = true,
    };
    int64_t lifetime;
    uint32_t result = 0U;
    int rc;

    rc = sirena_lwm2m_get_integer(SIRENA_LWM2M_SERVER_OBJECT_ID, 0U,
                                  SIRENA_LWM2M_SERVER_LIFETIME_RESOURCE, &lifetime);
    if (rc != 0) {
        return rc;
    }

    if (lifetime < 0) {
        return -EINVAL;
    }

    message.lifetime = (uint32_t)lifetime;

    rc = mqtt_set_text(message.binding, sizeof(message.binding), SIRENA_LWM2M_MQTT_BINDING_MODE);
    if (rc != 0) {
        return rc;
    }

    return mqtt_request(&message, SIRENA_LWM2M_MQTT_RESULT_CHANGED, &result);
}

static int mqtt_deregister(void)
{
    struct sirena_lwm2m_mqtt_message message = {
        .has_operation = true,
        .operation = SIRENA_LWM2M_MQTT_OPERATION_DEREGISTER,
        .token = 0U,
    };
    uint32_t result = 0U;

    return mqtt_request(&message, SIRENA_LWM2M_MQTT_RESULT_DEREGISTERED, &result);
}

static int mqtt_send(const struct sirena_lwm2m_send_record *records, size_t count)
{
    struct sirena_lwm2m_mqtt_message message = {
        .has_operation = true,
        .operation = SIRENA_LWM2M_MQTT_OPERATION_SEND,
        .token = 0U,
        .has_content_format = true,
        .content_format = SIRENA_LWM2M_CF_SENML_JSON,
        .has_payload = true,
    };
    const char *endpoint_name = sirena_lwm2m_transport_endpoint_name();
    size_t length = sizeof(value_buffer);
    uint32_t result = 0U;
    int rc;

    if (records == NULL || count == 0U) {
        return -EINVAL;
    }

    rc = sirena_lwm2m_protocol_send_encode(records, count, value_buffer, &length);
    if (rc != 0) {
        return rc;
    }

    if (endpoint_name[0] == '\0') {
        return -EINVAL;
    }

    message.payload = value_buffer;
    message.payload_length = length;

    return mqtt_request(&message, SIRENA_LWM2M_MQTT_RESULT_CHANGED, &result);
}

static int mqtt_poll(int timeout_ms)
{
    int rc;

    if (!atomic_get(&transport_started)) {
        return 0;
    }

    rc = mqtt_input(&client);
    if (rc < 0) {
        return -EIO;
    }

    (void)mqtt_live(&client);

    {
        size_t i;
        bool handled[ARRAY_SIZE(observations)] = { false };

        for (i = 0U; i < ARRAY_SIZE(observations); ++i) {
            struct sirena_lwm2m_mqtt_observation *observation = &observations[i];
            struct sirena_lwm2m_resource_attributes attrs;
            int64_t now;
            size_t j;
            bool already = false;

            if (!observation->used || observation->last_notify_ms == 0) {
                continue;
            }

            if (sirena_lwm2m_get_resource_attributes(observation->object_id,
                                                     observation->instance_id,
                                                     observation->resource_id,
                                                     &attrs) != 0 || !attrs.has_pmax) {
                continue;
            }

            now = k_uptime_get();
            if (now - observation->last_notify_ms < (int64_t)attrs.pmax * 1000) {
                continue;
            }

            for (j = 0U; j < i; ++j) {
                if (handled[j] && observations[j].used &&
                    observations[j].object_id == observation->object_id &&
                    observations[j].instance_id == observation->instance_id &&
                    observations[j].resource_id == observation->resource_id) {
                    already = true;
                    break;
                }
            }
            if (already) {
                continue;
            }

            (void)mqtt_notify(observation->object_id, observation->instance_id,
                              observation->resource_id);
            handled[i] = true;
        }
    }

    k_msleep(timeout_ms);

    return 0;
}

static int mqtt_notify(uint16_t object_id, uint16_t instance_id, uint16_t resource_id)
{
    struct sirena_lwm2m_mqtt_message message = {
        .has_operation = true,
        .operation = SIRENA_LWM2M_MQTT_OPERATION_NOTIFY,
        .token = 0U,
        .has_content_format = true,
        .has_payload = true,
    };
    size_t i;
    int sent = 0;

    for (i = 0U; i < ARRAY_SIZE(observations); ++i) {
        struct sirena_lwm2m_mqtt_observation *observation = &observations[i];
        size_t length = sizeof(value_buffer);
        size_t encoded_length = sizeof(message_buffer);
        int rc;

        if (!observation->used) {
            continue;
        }

        if (observation->object_id != object_id || observation->instance_id != instance_id ||
            observation->resource_id != resource_id) {
            continue;
        }

        {
            struct sirena_lwm2m_resource_attributes notify_attrs;
            int64_t now = k_uptime_get();

            if (sirena_lwm2m_get_resource_attributes(object_id, instance_id, resource_id,
                                                     &notify_attrs) == 0 &&
                notify_attrs.has_pmin && observation->last_notify_ms != 0 &&
                (now - observation->last_notify_ms) < (int64_t)notify_attrs.pmin * 1000) {
                continue;
            }
        }

        rc = sirena_lwm2m_protocol_read(object_id, instance_id, resource_id,
                                        observation->content_format, value_buffer, &length);
        if (rc != 0) {
            continue;
        }

        message.token = observation->token;
        message.content_format = observation->content_format;
        message.payload = value_buffer;
        message.payload_length = length;

        if (sirena_lwm2m_mqtt_message_encode(&message, message_buffer, &encoded_length) != 0) {
            continue;
        }

        if (mqtt_publish_payload(message_buffer, encoded_length) == 0) {
            observation->last_notify_ms = k_uptime_get();
            sent++;
        }
    }

    return sent;
}

const struct sirena_lwm2m_transport_ops sirena_lwm2m_mqtt_transport_ops = {
    .register_client = mqtt_register,
    .update = mqtt_update,
    .deregister = mqtt_deregister,
    .poll = mqtt_poll,
    .notify = mqtt_notify,
    .send = mqtt_send,
};

int sirena_lwm2m_mqtt_set_client_id(const char *id)
{
    size_t length;

    if (id == NULL) {
        return -EINVAL;
    }

    if (atomic_get(&transport_started)) {
        return -EBUSY;
    }

    length = strlen(id);
    if (length > CONFIG_SIRENA_LWM2M_MQTT_CLIENT_ID_MAX) {
        return -EINVAL;
    }

    memcpy(client_id, id, length + 1U);

    return 0;
}

int sirena_lwm2m_mqtt_set_topic_prefix(const char *prefix)
{
    size_t length;

    if (prefix == NULL) {
        return -EINVAL;
    }

    if (atomic_get(&transport_started)) {
        return -EBUSY;
    }

    length = strlen(prefix);
    if (length > CONFIG_SIRENA_LWM2M_MQTT_TOPIC_PREFIX_MAX) {
        return -EINVAL;
    }

    memcpy(topic_prefix, prefix, length + 1U);

    return 0;
}

int sirena_lwm2m_mqtt_start_engine(void)
{
    struct mqtt_subscription_list subscription = {
        .list_count = 1U,
        .message_id = 1U,
    };
    struct mqtt_topic subscription_topic = {
        .qos = MQTT_QOS_0_AT_MOST_ONCE,
    };
    enum sirena_lwm2m_uri_scheme scheme;
    int rc;

    if (atomic_get(&transport_started)) {
        return 0;
    }

    rc = mqtt_transport_scheme(&scheme);
    if (rc != 0) {
        return rc;
    }

    rc = mqtt_security_mode_check(scheme);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_protocol_init();
    if (rc != 0) {
        return rc;
    }

    rc = mqtt_build_topic();
    if (rc != 0) {
        return rc;
    }

    rc = mqtt_build_client_id();
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_uri_server_address(&broker_address, scheme);
    if (rc != 0) {
        return rc;
    }

    memset(observations, 0, sizeof(observations));
    connack_received = false;
    connack_code = 0U;
    response_received = false;
    pending_token = 0U;

    mqtt_client_init(&client);
    client.client_id.utf8 = (const uint8_t *)client_id;
    client.client_id.size = (uint32_t)strlen(client_id);
    client.broker = net_sad(&broker_address);
    client.protocol_version = MQTT_VERSION_3_1_1;
    client.rx_buf = rx_buffer;
    client.rx_buf_size = sizeof(rx_buffer);
    client.tx_buf = tx_buffer;
    client.tx_buf_size = sizeof(tx_buffer);
    client.evt_cb = mqtt_event_handler;

    rc = mqtt_transport_setup(scheme);
    if (rc != 0) {
        return rc;
    }

    rc = mqtt_connect(&client);
    if (rc != 0) {
        sirena_lwm2m_credentials_release(CONFIG_SIRENA_LWM2M_MQTT_SEC_TAG);

        return rc;
    }

    rc = mqtt_wait_connack();
    if (rc != 0) {
        (void)mqtt_abort(&client);
        sirena_lwm2m_credentials_release(CONFIG_SIRENA_LWM2M_MQTT_SEC_TAG);

        return rc;
    }

    subscription_topic.topic.utf8 = (const uint8_t *)topic;
    subscription_topic.topic.size = (uint32_t)strlen(topic);
    subscription.list = &subscription_topic;

    rc = mqtt_subscribe(&client, &subscription);
    if (rc != 0) {
        (void)mqtt_abort(&client);
        sirena_lwm2m_credentials_release(CONFIG_SIRENA_LWM2M_MQTT_SEC_TAG);

        return rc;
    }

    atomic_set(&transport_started, 1);

    return 0;
}

int sirena_lwm2m_mqtt_stop_engine(void)
{
    if (atomic_get(&transport_started)) {
        (void)mqtt_disconnect(&client, NULL);
    }

    sirena_lwm2m_credentials_release(CONFIG_SIRENA_LWM2M_MQTT_SEC_TAG);
    atomic_clear(&transport_started);
    memset(observations, 0, sizeof(observations));
    topic[0] = '\0';
    response_received = false;
    pending_token = 0U;

    return 0;
}

int sirena_lwm2m_mqtt_start(void)
{
    int rc;

    rc = sirena_lwm2m_transport_engine_ensure();
    if (rc != 0) {
        return rc;
    }

    return sirena_lwm2m_engine_submit(SIRENA_LWM2M_ENGINE_OPERATION_START_MQTT, NULL);
}

int sirena_lwm2m_mqtt_stop(void)
{
    if (sirena_lwm2m_engine_thread_id() == NULL) {
        return 0;
    }

    return sirena_lwm2m_engine_submit(SIRENA_LWM2M_ENGINE_OPERATION_STOP_MQTT, NULL);
}
