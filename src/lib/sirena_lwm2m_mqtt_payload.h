#ifndef SIRENA_LWM2M_MQTT_PAYLOAD_H
#define SIRENA_LWM2M_MQTT_PAYLOAD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SIRENA_LWM2M_MQTT_TEXT_MAX 64U

enum sirena_lwm2m_mqtt_operation {
    SIRENA_LWM2M_MQTT_OPERATION_REGISTER = 6,
    SIRENA_LWM2M_MQTT_OPERATION_UPDATE = 7,
    SIRENA_LWM2M_MQTT_OPERATION_DEREGISTER = 8,
    SIRENA_LWM2M_MQTT_OPERATION_READ = 9,
    SIRENA_LWM2M_MQTT_OPERATION_DISCOVER = 11,
    SIRENA_LWM2M_MQTT_OPERATION_WRITE = 12,
    SIRENA_LWM2M_MQTT_OPERATION_WRITE_ATTRIBUTES = 13,
    SIRENA_LWM2M_MQTT_OPERATION_EXECUTE = 16,
    SIRENA_LWM2M_MQTT_OPERATION_OBSERVE = 20,
    SIRENA_LWM2M_MQTT_OPERATION_CANCEL_OBSERVE = 22,
    SIRENA_LWM2M_MQTT_OPERATION_NOTIFY = 23,
};

enum sirena_lwm2m_mqtt_result {
    SIRENA_LWM2M_MQTT_RESULT_REGISTERED = 201,
    SIRENA_LWM2M_MQTT_RESULT_DEREGISTERED = 202,
    SIRENA_LWM2M_MQTT_RESULT_CHANGED = 204,
    SIRENA_LWM2M_MQTT_RESULT_CONTENT = 205,
    SIRENA_LWM2M_MQTT_RESULT_BAD_REQUEST = 400,
    SIRENA_LWM2M_MQTT_RESULT_FORBIDDEN = 403,
    SIRENA_LWM2M_MQTT_RESULT_NOT_FOUND = 404,
    SIRENA_LWM2M_MQTT_RESULT_NOT_ALLOWED = 405,
    SIRENA_LWM2M_MQTT_RESULT_NOT_ACCEPTABLE = 406,
    SIRENA_LWM2M_MQTT_RESULT_CONFLICT = 409,
    SIRENA_LWM2M_MQTT_RESULT_PRECONDITION_FAILED = 412,
    SIRENA_LWM2M_MQTT_RESULT_UNSUPPORTED_FORMAT = 415,
    SIRENA_LWM2M_MQTT_RESULT_INTERNAL_ERROR = 500,
    SIRENA_LWM2M_MQTT_RESULT_NOT_IMPLEMENTED = 501,
    SIRENA_LWM2M_MQTT_RESULT_UNAVAILABLE = 503,
};

struct sirena_lwm2m_mqtt_message {
    bool has_operation;
    uint32_t operation;
    uint32_t token;
    bool has_result;
    uint32_t result;
    bool has_lifetime;
    uint32_t lifetime;
    bool has_content_format;
    uint32_t content_format;
    bool has_endpoint;
    char endpoint[SIRENA_LWM2M_MQTT_TEXT_MAX + 1U];
    bool has_version;
    char version[SIRENA_LWM2M_MQTT_TEXT_MAX + 1U];
    bool has_binding;
    char binding[SIRENA_LWM2M_MQTT_TEXT_MAX + 1U];
    bool has_uri;
    char uri[SIRENA_LWM2M_MQTT_TEXT_MAX + 1U];
    bool has_payload;
    const uint8_t *payload;
    size_t payload_length;
};

int sirena_lwm2m_mqtt_message_decode(const void *data, size_t length,
                                     struct sirena_lwm2m_mqtt_message *message);
int sirena_lwm2m_mqtt_message_encode(const struct sirena_lwm2m_mqtt_message *message,
                                     void *buffer, size_t *length);

#endif
