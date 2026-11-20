#include <sirena/lwm2m/protocol.h>
#include <sirena/lwm2m/codec.h>
#include <sirena/lwm2m/engine.h>
#include <sirena/lwm2m/object_registry.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <string.h>

#define SIRENA_LWM2M_PREFERRED_CONTENT_FORMAT CONFIG_SIRENA_LWM2M_PREFERRED_CONTENT_FORMAT

static uint8_t protocol_raw[CONFIG_SIRENA_LWM2M_PROTOCOL_VALUE_MAX];

static int protocol_value_from_resource(const struct sirena_lwm2m_resource *resource,
                                        const void *raw, size_t raw_length,
                                        struct sirena_lwm2m_codec_value *value)
{
    memset(value, 0, sizeof(*value));

    switch (resource->type) {
    case SIRENA_LWM2M_VALUE_BOOLEAN:
        if (raw_length < sizeof(bool)) {
            return -EINVAL;
        }
        value->type = SIRENA_LWM2M_VALUE_BOOLEAN;
        memcpy(&value->data.boolean, raw, sizeof(bool));
        return 0;
    case SIRENA_LWM2M_VALUE_INTEGER:
        if (raw_length < sizeof(int64_t)) {
            return -EINVAL;
        }
        value->type = SIRENA_LWM2M_VALUE_INTEGER;
        memcpy(&value->data.integer, raw, sizeof(int64_t));
        return 0;
    case SIRENA_LWM2M_VALUE_UNSIGNED:
        if (raw_length < sizeof(uint64_t)) {
            return -EINVAL;
        }
        value->type = SIRENA_LWM2M_VALUE_UNSIGNED;
        memcpy(&value->data.unsigned_integer, raw, sizeof(uint64_t));
        return 0;
    case SIRENA_LWM2M_VALUE_FLOAT:
        if (raw_length < sizeof(double)) {
            return -EINVAL;
        }
        value->type = SIRENA_LWM2M_VALUE_FLOAT;
        memcpy(&value->data.floating_point, raw, sizeof(double));
        return 0;
    case SIRENA_LWM2M_VALUE_STRING:
        value->type = SIRENA_LWM2M_VALUE_STRING;
        value->data.string.value = raw;
        value->data.string.length = raw_length;
        return 0;
    case SIRENA_LWM2M_VALUE_OPAQUE:
        value->type = SIRENA_LWM2M_VALUE_OPAQUE;
        value->data.opaque.value = raw;
        value->data.opaque.length = raw_length;
        return 0;
    default:
        return -ENOTSUP;
    }
}

int sirena_lwm2m_protocol_read(uint16_t object_id, uint16_t instance_id, uint16_t resource_id,
                               uint16_t content_format, void *buffer, size_t *length)
{
    struct sirena_lwm2m_resource *resource;
    struct sirena_lwm2m_codec_value value;
    size_t raw_length = sizeof(protocol_raw);
    int rc;

    if (buffer == NULL || length == NULL) {
        return -EINVAL;
    }

    rc = sirena_lwm2m_find_resource(object_id, instance_id, resource_id, &resource);
    if (rc != 0) {
        return rc;
    }

    if ((resource->operations & SIRENA_LWM2M_OP_READ) == 0U || resource->read == NULL) {
        return -ENOTSUP;
    }

    rc = sirena_lwm2m_get(object_id, instance_id, resource_id, protocol_raw, &raw_length);
    if (rc != 0) {
        return rc;
    }

    rc = protocol_value_from_resource(resource, protocol_raw, raw_length, &value);
    if (rc != 0) {
        return rc;
    }

    value.object_id = object_id;
    value.instance_id = instance_id;
    value.resource_id = resource_id;

    return sirena_lwm2m_codec_encode(content_format, &value, 1U, buffer, length);
}

static int protocol_write_resource(uint16_t object_id, uint16_t instance_id,
                                   uint16_t resource_id,
                                   const struct sirena_lwm2m_resource *resource,
                                   const struct sirena_lwm2m_codec_value *value)
{
    switch (resource->type) {
    case SIRENA_LWM2M_VALUE_BOOLEAN: {
        bool state;

        if (value->type == SIRENA_LWM2M_VALUE_BOOLEAN) {
            state = value->data.boolean;
        } else if (value->type == SIRENA_LWM2M_VALUE_INTEGER) {
            state = value->data.integer != 0;
        } else {
            return -EINVAL;
        }

        return sirena_lwm2m_set(object_id, instance_id, resource_id, &state, sizeof(state));
    }
    case SIRENA_LWM2M_VALUE_INTEGER:
        if (value->type != SIRENA_LWM2M_VALUE_INTEGER) {
            return -EINVAL;
        }

        return sirena_lwm2m_set(object_id, instance_id, resource_id, &value->data.integer,
                                sizeof(value->data.integer));
    case SIRENA_LWM2M_VALUE_UNSIGNED:
        if (value->type != SIRENA_LWM2M_VALUE_UNSIGNED) {
            return -EINVAL;
        }

        return sirena_lwm2m_set(object_id, instance_id, resource_id, &value->data.unsigned_integer,
                                sizeof(value->data.unsigned_integer));
    case SIRENA_LWM2M_VALUE_FLOAT:
        if (value->type != SIRENA_LWM2M_VALUE_FLOAT) {
            return -EINVAL;
        }

        return sirena_lwm2m_set(object_id, instance_id, resource_id, &value->data.floating_point,
                                sizeof(value->data.floating_point));
    case SIRENA_LWM2M_VALUE_STRING:
        if (value->type != SIRENA_LWM2M_VALUE_STRING &&
            value->type != SIRENA_LWM2M_VALUE_OPAQUE) {
            return -EINVAL;
        }

        return sirena_lwm2m_set(object_id, instance_id, resource_id, value->data.string.value,
                                value->data.string.length);
    case SIRENA_LWM2M_VALUE_OPAQUE:
        if (value->type != SIRENA_LWM2M_VALUE_OPAQUE &&
            value->type != SIRENA_LWM2M_VALUE_STRING) {
            return -EINVAL;
        }

        return sirena_lwm2m_set(object_id, instance_id, resource_id, value->data.opaque.value,
                                value->data.opaque.length);
    default:
        return -ENOTSUP;
    }
}

static int protocol_negotiate(const struct sirena_lwm2m_protocol_request *request,
                               uint16_t *content_format)
{
    uint16_t preferred = SIRENA_LWM2M_PREFERRED_CONTENT_FORMAT;
    int rc;

    if (preferred == 0U && (request->accepted_formats == NULL ||
                            request->accepted_format_count == 0U)) {
        *content_format = preferred;
        return 0;
    }

    rc = sirena_lwm2m_content_format_negotiate(request->accepted_formats,
                                                 request->accepted_format_count, preferred);
    if (rc < 0) {
        return rc;
    }

    *content_format = (uint16_t)rc;

    return 0;
}

static void protocol_set_status(struct sirena_lwm2m_protocol_response *response,
                                enum sirena_lwm2m_status status)
{
    response->status = status;
    response->content_format = 0U;
    response->payload_length = 0U;
}

int sirena_lwm2m_protocol_init(void)
{
    if (!sirena_lwm2m_content_format_supported(SIRENA_LWM2M_PREFERRED_CONTENT_FORMAT)) {
        return -EINVAL;
    }

    return 0;
}

int sirena_lwm2m_protocol_execute(const struct sirena_lwm2m_protocol_request *request,
                                  struct sirena_lwm2m_protocol_response *response)
{
    struct sirena_lwm2m_resource *resource;
    uint16_t content_format = 0U;
    int rc;

    if (request == NULL || response == NULL) {
        return -EINVAL;
    }

    response->status = SIRENA_LWM2M_STATUS_INTERNAL_ERROR;
    response->content_format = 0U;
    response->payload_length = 0U;

    rc = sirena_lwm2m_find_resource(request->object_id, request->instance_id,
                                     request->resource_id, &resource);
    if (rc != 0) {
        protocol_set_status(response, SIRENA_LWM2M_STATUS_NOT_FOUND);
        return 0;
    }

    rc = protocol_negotiate(request, &content_format);
    if (rc != 0) {
        protocol_set_status(response, SIRENA_LWM2M_STATUS_NOT_ACCEPTABLE);
        return 0;
    }

    switch (request->operation) {
    case SIRENA_LWM2M_OPERATION_READ: {
        size_t length = response->payload_capacity;

        if ((resource->operations & SIRENA_LWM2M_OP_READ) == 0U || resource->read == NULL) {
            protocol_set_status(response, SIRENA_LWM2M_STATUS_METHOD_NOT_ALLOWED);
            return 0;
        }

        rc = sirena_lwm2m_protocol_read(request->object_id, request->instance_id,
                                        request->resource_id, content_format,
                                        response->payload, &length);
        if (rc != 0) {
            protocol_set_status(response, SIRENA_LWM2M_STATUS_INTERNAL_ERROR);
            return 0;
        }

        response->status = SIRENA_LWM2M_STATUS_CONTENT;
        response->content_format = content_format;
        response->payload_length = length;
        return 0;
    }
    case SIRENA_LWM2M_OPERATION_WRITE: {
        struct sirena_lwm2m_codec_value values[CONFIG_SIRENA_LWM2M_CODEC_MAX_VALUES];
        uint16_t write_format;
        size_t value_count = ARRAY_SIZE(values);

        if ((resource->operations & SIRENA_LWM2M_OP_WRITE) == 0U || resource->write == NULL) {
            protocol_set_status(response, SIRENA_LWM2M_STATUS_METHOD_NOT_ALLOWED);
            return 0;
        }

        write_format = request->content_format_present ? request->content_format : content_format;

        rc = sirena_lwm2m_codec_decode(write_format, request->payload, request->payload_length,
                                       values, &value_count);
        if (rc != 0) {
            protocol_set_status(response,
                                rc == -ENOTSUP ? SIRENA_LWM2M_STATUS_UNSUPPORTED_CONTENT_FORMAT
                                               : SIRENA_LWM2M_STATUS_BAD_REQUEST);
            return 0;
        }

        if (value_count != 1U) {
            protocol_set_status(response, SIRENA_LWM2M_STATUS_BAD_REQUEST);
            return 0;
        }

        rc = protocol_write_resource(request->object_id, request->instance_id,
                                     request->resource_id, resource, &values[0]);
        if (rc == -ENOTSUP) {
            protocol_set_status(response, SIRENA_LWM2M_STATUS_METHOD_NOT_ALLOWED);
            return 0;
        }

        if (rc != 0) {
            protocol_set_status(response, SIRENA_LWM2M_STATUS_BAD_REQUEST);
            return 0;
        }

        protocol_set_status(response, SIRENA_LWM2M_STATUS_CHANGED);
        return 0;
    }
    case SIRENA_LWM2M_OPERATION_EXECUTE:
        if ((resource->operations & SIRENA_LWM2M_OP_EXEC) == 0U || resource->execute == NULL) {
            protocol_set_status(response, SIRENA_LWM2M_STATUS_METHOD_NOT_ALLOWED);
            return 0;
        }

        rc = sirena_lwm2m_execute(request->object_id, request->instance_id,
                                  request->resource_id, request->payload,
                                  request->payload_length);
        if (rc == -ENOTSUP) {
            protocol_set_status(response, SIRENA_LWM2M_STATUS_METHOD_NOT_ALLOWED);
            return 0;
        }

        if (rc != 0) {
            protocol_set_status(response, SIRENA_LWM2M_STATUS_INTERNAL_ERROR);
            return 0;
        }

        protocol_set_status(response, SIRENA_LWM2M_STATUS_CHANGED);
        return 0;
    default:
        protocol_set_status(response, SIRENA_LWM2M_STATUS_METHOD_NOT_ALLOWED);
        return 0;
    }
}

int sirena_lwm2m_notify_resource(uint16_t object_id, uint16_t instance_id,
                                 uint16_t resource_id)
{
    struct sirena_lwm2m_notify_request notify = {
        .object_id = object_id,
        .instance_id = instance_id,
        .resource_id = resource_id,
    };

    return sirena_lwm2m_engine_submit(SIRENA_LWM2M_ENGINE_OPERATION_NOTIFY, &notify);
}
