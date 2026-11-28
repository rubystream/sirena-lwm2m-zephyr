#include <sirena/lwm2m/protocol.h>
#include <sirena/lwm2m/codec.h>
#include <sirena/lwm2m/engine.h>
#include <sirena/lwm2m/object_registry.h>
#include <sirena/lwm2m/objects.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define SIRENA_LWM2M_PREFERRED_CONTENT_FORMAT CONFIG_SIRENA_LWM2M_PREFERRED_CONTENT_FORMAT

static uint8_t protocol_raw[CONFIG_SIRENA_LWM2M_PROTOCOL_VALUE_MAX];
static struct sirena_lwm2m_codec_value protocol_values[CONFIG_SIRENA_LWM2M_CODEC_MAX_VALUES];
static uint8_t protocol_send_raw[CONFIG_SIRENA_LWM2M_CODEC_MAX_VALUES][CONFIG_SIRENA_LWM2M_PROTOCOL_VALUE_MAX];
static struct sirena_lwm2m_codec_value protocol_send_values[CONFIG_SIRENA_LWM2M_CODEC_MAX_VALUES];
static struct sirena_lwm2m_senml_record protocol_composite_records[CONFIG_SIRENA_LWM2M_CODEC_MAX_VALUES];
static struct sirena_lwm2m_codec_value protocol_composite_status[CONFIG_SIRENA_LWM2M_CODEC_MAX_VALUES];

int sirena_lwm2m_protocol_value_from_resource(const struct sirena_lwm2m_resource *resource,
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

    rc = sirena_lwm2m_protocol_value_from_resource(resource, protocol_raw, raw_length, &value);
    if (rc != 0) {
        return rc;
    }

    value.object_id = object_id;
    value.instance_id = instance_id;
    value.resource_id = resource_id;
    value.id = resource_id;

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
                               const struct sirena_lwm2m_resource *resource,
                               uint16_t *content_format)
{
    int rc;

    rc = sirena_lwm2m_content_format_negotiate(request->accepted_formats,
                                                 request->accepted_format_count,
                                                 SIRENA_LWM2M_PREFERRED_CONTENT_FORMAT,
                                                 resource->type);
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

static struct {
    uint16_t object_id;
    const struct sirena_lwm2m_resource_attributes *incoming;
    bool any;
    bool failed;
} wa_ctx;

static int protocol_apply_attributes(uint16_t object_id, uint16_t instance_id, uint16_t resource_id,
                                     const struct sirena_lwm2m_resource_attributes *incoming)
{
    struct sirena_lwm2m_resource_attributes attrs;
    int rc;

    rc = sirena_lwm2m_get_resource_attributes(object_id, instance_id, resource_id, &attrs);
    if (rc != 0) {
        return rc;
    }

    if (incoming->has_pmin) {
        attrs.has_pmin = true;
        attrs.pmin = incoming->pmin;
    }
    if (incoming->has_pmax) {
        attrs.has_pmax = true;
        attrs.pmax = incoming->pmax;
    }
    if (incoming->has_gt) {
        attrs.has_gt = true;
        attrs.gt = incoming->gt;
    }
    if (incoming->has_lt) {
        attrs.has_lt = true;
        attrs.lt = incoming->lt;
    }
    if (incoming->has_st) {
        attrs.has_st = true;
        attrs.st = incoming->st;
    }

    return sirena_lwm2m_set_resource_attributes(object_id, instance_id, resource_id, &attrs);
}

static int wa_instance_visitor(uint16_t object_id, uint16_t instance_id, void *user_data)
{
    struct sirena_lwm2m_instance *inst;
    size_t i;
    int rc;

    (void)user_data;

    if (object_id != wa_ctx.object_id) {
        return 0;
    }

    rc = sirena_lwm2m_find_instance(object_id, instance_id, &inst);
    if (rc != 0) {
        return 0;
    }

    for (i = 0U; i < inst->resource_count; ++i) {
        rc = protocol_apply_attributes(object_id, instance_id, inst->resources[i].id,
                                      wa_ctx.incoming);
        if (rc == -ENOENT) {
            continue;
        }
        if (rc != 0) {
            wa_ctx.failed = true;
            return -1;
        }
        wa_ctx.any = true;
    }

    return 0;
}

int sirena_lwm2m_protocol_init(void)
{
    if (!sirena_lwm2m_content_format_supported(SIRENA_LWM2M_PREFERRED_CONTENT_FORMAT)) {
        return -EINVAL;
    }

    return 0;
}

struct discover_context {
    uint16_t object_id;
    char *buffer;
    size_t capacity;
    size_t length;
    bool any;
};

static int discover_append_entry(struct discover_context *context, uint16_t object_id,
                                 uint16_t instance_id, uint16_t resource_id, bool resource_path)
{
    int written;
    const char *prefix = context->length > 0U ? "," : "";

    if (resource_path) {
        written = snprintf(context->buffer + context->length, context->capacity - context->length,
                           "%s</%u/%u/%u>", prefix, object_id, instance_id, resource_id);
    } else if (instance_id != UINT16_MAX) {
        (void)resource_id;
        written = snprintf(context->buffer + context->length, context->capacity - context->length,
                           "%s</%u/%u>", prefix, object_id, instance_id);
    } else {
        (void)resource_id;
        written = -1;
    }

    if (written < 0 || (size_t)written >= context->capacity - context->length) {
        return -ENOMEM;
    }

    context->length += (size_t)written;

    if (resource_path) {
        struct sirena_lwm2m_resource_attributes attrs;

        if (sirena_lwm2m_get_resource_attributes(object_id, instance_id, resource_id,
                                                 &attrs) == 0) {
            if (attrs.has_pmin) {
                written = snprintf(context->buffer + context->length,
                                   context->capacity - context->length, ";pmin=%u", attrs.pmin);
                if (written < 0 || (size_t)written >= context->capacity - context->length) {
                    return -ENOMEM;
                }
                context->length += (size_t)written;
            }
            if (attrs.has_pmax) {
                written = snprintf(context->buffer + context->length,
                                   context->capacity - context->length, ";pmax=%u", attrs.pmax);
                if (written < 0 || (size_t)written >= context->capacity - context->length) {
                    return -ENOMEM;
                }
                context->length += (size_t)written;
            }
            if (attrs.has_gt) {
                written = snprintf(context->buffer + context->length,
                                   context->capacity - context->length, ";gt=%g", attrs.gt);
                if (written < 0 || (size_t)written >= context->capacity - context->length) {
                    return -ENOMEM;
                }
                context->length += (size_t)written;
            }
            if (attrs.has_lt) {
                written = snprintf(context->buffer + context->length,
                                   context->capacity - context->length, ";lt=%g", attrs.lt);
                if (written < 0 || (size_t)written >= context->capacity - context->length) {
                    return -ENOMEM;
                }
                context->length += (size_t)written;
            }
            if (attrs.has_st) {
                written = snprintf(context->buffer + context->length,
                                   context->capacity - context->length, ";st=%g", attrs.st);
                if (written < 0 || (size_t)written >= context->capacity - context->length) {
                    return -ENOMEM;
                }
                context->length += (size_t)written;
            }
        }
    }

    context->any = true;

    return 0;
}

static int discover_instance_visitor(uint16_t object_id, uint16_t instance_id, void *user_data)
{
    struct discover_context *context = user_data;

    if (object_id != context->object_id) {
        return 0;
    }

    return discover_append_entry(context, object_id, instance_id, 0U, false);
}

static int protocol_parse_path(const char *path, uint16_t *object_id, uint16_t *instance_id,
                               uint16_t *resource_id)
{
    unsigned long parts[3] = {0, 0, 0};
    size_t part = 0U;
    size_t pos = 0U;
    size_t length = strlen(path);
    unsigned long value = 0U;
    bool any = false;

    while (pos <= length) {
        if (pos == length || path[pos] == '/') {
            if (any) {
                if (part >= 3U) {
                    return -EINVAL;
                }
                parts[part++] = value;
                value = 0U;
                any = false;
            }
        } else {
            if (path[pos] < '0' || path[pos] > '9') {
                return -EINVAL;
            }
            value = value * 10UL + (unsigned long)(path[pos] - '0');
            if (value > 65535UL) {
                return -EINVAL;
            }
            any = true;
        }
        pos++;
    }

    if (part != 3U) {
        return -EINVAL;
    }

    *object_id = (uint16_t)parts[0];
    *instance_id = (uint16_t)parts[1];
    *resource_id = (uint16_t)parts[2];

    return 0;
}

static int protocol_value_from_text(enum sirena_lwm2m_value_type type, const char *text,
                                    size_t length, struct sirena_lwm2m_codec_value *value)
{
    memset(value, 0, sizeof(*value));
    value->type = type;

    switch (type) {
    case SIRENA_LWM2M_VALUE_BOOLEAN:
        if (length == 4U && strncmp(text, "true", 4U) == 0) {
            value->data.boolean = true;
            return 0;
        }
        if (length == 5U && strncmp(text, "false", 5U) == 0) {
            value->data.boolean = false;
            return 0;
        }
        return -EINVAL;
    case SIRENA_LWM2M_VALUE_INTEGER: {
        size_t i = 0U;
        bool negative = false;
        int64_t number = 0;

        if (length == 0U) {
            return -EINVAL;
        }
        if (text[0] == '-') {
            negative = true;
            i = 1U;
        }
        if (i >= length || text[i] < '0' || text[i] > '9') {
            return -EINVAL;
        }
        for (; i < length; i++) {
            if (text[i] < '0' || text[i] > '9') {
                return -EINVAL;
            }
            number = number * 10 + (int64_t)(text[i] - '0');
        }
        value->data.integer = negative ? -number : number;
        return 0;
    }
    case SIRENA_LWM2M_VALUE_UNSIGNED: {
        size_t i;
        uint64_t number = 0;

        if (length == 0U) {
            return -EINVAL;
        }
        for (i = 0U; i < length; i++) {
            if (text[i] < '0' || text[i] > '9') {
                return -EINVAL;
            }
            number = number * 10U + (uint64_t)(text[i] - '0');
        }
        value->data.unsigned_integer = number;
        return 0;
    }
    case SIRENA_LWM2M_VALUE_FLOAT: {
        char buffer[64];
        char *end = NULL;
        double number;

        if (length == 0U || length >= sizeof(buffer)) {
            return -EINVAL;
        }
        memcpy(buffer, text, length);
        buffer[length] = '\0';
        number = strtod(buffer, &end);
        if (end == buffer || *end != '\0') {
            return -EINVAL;
        }
        value->data.floating_point = number;
        return 0;
    }
    case SIRENA_LWM2M_VALUE_STRING:
        value->data.string.value = text;
        value->data.string.length = length;
        return 0;
    case SIRENA_LWM2M_VALUE_OPAQUE:
        value->data.opaque.value = text;
        value->data.opaque.length = length;
        return 0;
    default:
        return -ENOTSUP;
    }
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
    if (rc != 0 && request->operation != SIRENA_LWM2M_OPERATION_CREATE &&
        request->operation != SIRENA_LWM2M_OPERATION_DELETE &&
        request->operation != SIRENA_LWM2M_OPERATION_WRITE_ATTRIBUTES &&
        request->operation != SIRENA_LWM2M_OPERATION_DISCOVER &&
        request->operation != SIRENA_LWM2M_OPERATION_WRITE_COMPOSITE) {
        protocol_set_status(response, SIRENA_LWM2M_STATUS_NOT_FOUND);
        return 0;
    }

    switch (request->operation) {
    case SIRENA_LWM2M_OPERATION_READ: {
        size_t length = response->payload_capacity;

        if ((resource->operations & SIRENA_LWM2M_OP_READ) == 0U || resource->read == NULL) {
            protocol_set_status(response, SIRENA_LWM2M_STATUS_METHOD_NOT_ALLOWED);
            return 0;
        }

        rc = protocol_negotiate(request, resource, &content_format);
        if (rc != 0) {
            protocol_set_status(response, SIRENA_LWM2M_STATUS_NOT_ACCEPTABLE);
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
        uint16_t write_format;
        size_t value_count = ARRAY_SIZE(protocol_values);

        if ((resource->operations & SIRENA_LWM2M_OP_WRITE) == 0U || resource->write == NULL) {
            protocol_set_status(response, SIRENA_LWM2M_STATUS_METHOD_NOT_ALLOWED);
            return 0;
        }

        write_format = request->content_format_present
                           ? request->content_format
                           : (uint16_t)SIRENA_LWM2M_PREFERRED_CONTENT_FORMAT;

        rc = sirena_lwm2m_codec_decode(write_format, resource->type,
                                       request->payload, request->payload_length,
                                       protocol_values, &value_count);
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
                                     request->resource_id, resource, &protocol_values[0]);
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
    case SIRENA_LWM2M_OPERATION_WRITE_COMPOSITE: {
        uint16_t write_format = request->content_format_present
                                   ? request->content_format
                                   : (uint16_t)SIRENA_LWM2M_CF_SENML_JSON;
        size_t record_count = ARRAY_SIZE(protocol_composite_records);
        size_t length;
        size_t i;

        if (write_format != (uint16_t)SIRENA_LWM2M_CF_SENML_JSON &&
            write_format != (uint16_t)SIRENA_LWM2M_CF_SENML_CBOR) {
            protocol_set_status(response, SIRENA_LWM2M_STATUS_UNSUPPORTED_CONTENT_FORMAT);
            return 0;
        }

        rc = sirena_lwm2m_codec_decode_senml_records(write_format, request->payload,
                                                      request->payload_length,
                                                      protocol_composite_records, &record_count);
        if (rc != 0 || record_count == 0U) {
            protocol_set_status(response, SIRENA_LWM2M_STATUS_BAD_REQUEST);
            return 0;
        }

        for (i = 0U; i < record_count; i++) {
            struct sirena_lwm2m_resource *resource;
            struct sirena_lwm2m_codec_value value;
            uint16_t object_id = 0U;
            uint16_t instance_id = 0U;
            uint16_t resource_id = 0U;
            int64_t status = 204;

            if (protocol_parse_path(protocol_composite_records[i].path, &object_id, &instance_id,
                                    &resource_id) != 0) {
                status = 404;
            } else if (sirena_lwm2m_find_resource(object_id, instance_id, resource_id,
                                                  &resource) != 0) {
                status = 404;
            } else if ((resource->operations & SIRENA_LWM2M_OP_WRITE) == 0U ||
                       resource->write == NULL) {
                status = 405;
            } else if (protocol_value_from_text(resource->type,
                                                protocol_composite_records[i].value,
                                                protocol_composite_records[i].value_length,
                                                &value) != 0) {
                status = 400;
            } else {
                int write_rc = protocol_write_resource(object_id, instance_id, resource_id,
                                                       resource, &value);

                if (write_rc == -ENOTSUP) {
                    status = 405;
                } else if (write_rc != 0) {
                    status = 500;
                }
            }

            protocol_composite_status[i].object_id = object_id;
            protocol_composite_status[i].instance_id = instance_id;
            protocol_composite_status[i].resource_id = resource_id;
            protocol_composite_status[i].id = resource_id;
            protocol_composite_status[i].type = SIRENA_LWM2M_VALUE_INTEGER;
            protocol_composite_status[i].data.integer = status;
        }

        length = response->payload_capacity;
        rc = sirena_lwm2m_codec_encode((uint16_t)SIRENA_LWM2M_CF_SENML_JSON,
                                       protocol_composite_status, record_count, response->payload,
                                       &length);
        if (rc != 0) {
            protocol_set_status(response, SIRENA_LWM2M_STATUS_INTERNAL_ERROR);
            return 0;
        }

        response->status = SIRENA_LWM2M_STATUS_CHANGED;
        response->content_format = (uint16_t)SIRENA_LWM2M_CF_SENML_JSON;
        response->payload_length = length;
        return 0;
    }
    case SIRENA_LWM2M_OPERATION_WRITE_ATTRIBUTES: {
        struct sirena_lwm2m_resource_attributes incoming = request->attributes;
        size_t pc = request->path_count == 0U ? 3U : request->path_count;
        size_t i;
        bool any = false;
        int last_rc = 0;

        if (pc >= 3U) {
            rc = protocol_apply_attributes(request->object_id, request->instance_id,
                                          request->resource_id, &incoming);
            if (rc == -ENOENT) {
                protocol_set_status(response, SIRENA_LWM2M_STATUS_NOT_FOUND);
                return 0;
            }
            if (rc != 0) {
                protocol_set_status(response, SIRENA_LWM2M_STATUS_BAD_REQUEST);
                return 0;
            }

            protocol_set_status(response, SIRENA_LWM2M_STATUS_CHANGED);
            return 0;
        }

        if (pc == 2U) {
            struct sirena_lwm2m_instance *inst;

            rc = sirena_lwm2m_find_instance(request->object_id, request->instance_id,
                                            &inst);
            if (rc != 0) {
                protocol_set_status(response, SIRENA_LWM2M_STATUS_NOT_FOUND);
                return 0;
            }

            for (i = 0U; i < inst->resource_count; ++i) {
                uint16_t rid = inst->resources[i].id;

                rc = protocol_apply_attributes(request->object_id, request->instance_id, rid,
                                             &incoming);
                if (rc == -ENOENT) {
                    last_rc = rc;
                    continue;
                }
                if (rc != 0) {
                    protocol_set_status(response, SIRENA_LWM2M_STATUS_BAD_REQUEST);
                    return 0;
                }
                any = true;
            }

            if (!any) {
                if (last_rc == -ENOENT) {
                    protocol_set_status(response, SIRENA_LWM2M_STATUS_NOT_FOUND);
                } else {
                    protocol_set_status(response, SIRENA_LWM2M_STATUS_BAD_REQUEST);
                }
                return 0;
            }

            protocol_set_status(response, SIRENA_LWM2M_STATUS_CHANGED);
            return 0;
        }

        wa_ctx.object_id = request->object_id;
        wa_ctx.incoming = &incoming;
        wa_ctx.any = false;
        wa_ctx.failed = false;

        rc = sirena_lwm2m_foreach_instance(wa_instance_visitor, NULL);

        if (wa_ctx.failed) {
            protocol_set_status(response, SIRENA_LWM2M_STATUS_BAD_REQUEST);
            return 0;
        }

        if (!wa_ctx.any) {
            protocol_set_status(response, SIRENA_LWM2M_STATUS_NOT_FOUND);
            return 0;
        }

        protocol_set_status(response, SIRENA_LWM2M_STATUS_CHANGED);
        return 0;
    }
    case SIRENA_LWM2M_OPERATION_CREATE: {
        uint16_t instance_id = 1U;
        int created_instance = -1;
        int applied = 0;

        while (instance_id < CONFIG_SIRENA_LWM2M_MAX_INSTANCES + 1U) {
            rc = sirena_lwm2m_create_object_instance(request->object_id, instance_id);
            if (rc == 0) {
                created_instance = instance_id;
                break;
            }
            if (rc != -EEXIST) {
                if (rc == -ENOMEM) {
                    protocol_set_status(response, SIRENA_LWM2M_STATUS_INTERNAL_ERROR);
                    return 0;
                }
                protocol_set_status(response, SIRENA_LWM2M_STATUS_NOT_FOUND);
                return 0;
            }
            instance_id += 1U;
        }

        if (created_instance < 0) {
            protocol_set_status(response, SIRENA_LWM2M_STATUS_INTERNAL_ERROR);
            return 0;
        }

        if (request->payload_length > 0U && request->resource_id != 0U) {
            uint16_t write_format = request->content_format_present
                                      ? request->content_format
                                      : (uint16_t)SIRENA_LWM2M_PREFERRED_CONTENT_FORMAT;
            size_t value_count = ARRAY_SIZE(protocol_values);
            struct sirena_lwm2m_resource *created_resource;

            rc = sirena_lwm2m_find_resource(request->object_id, created_instance,
                                             request->resource_id, &created_resource);
            if (rc == 0) {
                rc = sirena_lwm2m_codec_decode(write_format, created_resource->type,
                                               request->payload, request->payload_length,
                                               protocol_values, &value_count);
                if (rc == 0 && value_count >= 1U) {
                    rc = protocol_write_resource(request->object_id, created_instance,
                                                 request->resource_id, created_resource,
                                                 &protocol_values[0]);
                    if (rc != 0) {
                        sirena_lwm2m_delete_object_instance(request->object_id,
                                                           (uint16_t)created_instance);
                        protocol_set_status(response, SIRENA_LWM2M_STATUS_BAD_REQUEST);
                        return 0;
                    }
                    applied = 1;
                } else {
                    sirena_lwm2m_delete_object_instance(request->object_id,
                                                       (uint16_t)created_instance);
                    protocol_set_status(response, SIRENA_LWM2M_STATUS_BAD_REQUEST);
                    return 0;
                }
            }
        }

        (void)applied;
        protocol_set_status(response, SIRENA_LWM2M_STATUS_CREATED);
        return 0;
    }
    case SIRENA_LWM2M_OPERATION_DELETE: {
        rc = sirena_lwm2m_delete_object_instance(request->object_id,
                                                request->instance_id);
        if (rc == -ENOENT) {
            protocol_set_status(response, SIRENA_LWM2M_STATUS_NOT_FOUND);
            return 0;
        }
        if (rc != 0) {
            protocol_set_status(response, SIRENA_LWM2M_STATUS_INTERNAL_ERROR);
            return 0;
        }

        protocol_set_status(response, SIRENA_LWM2M_STATUS_DELETED);
        return 0;
    }
    case SIRENA_LWM2M_OPERATION_DISCOVER: {
        struct discover_context context = {
            .object_id = request->object_id,
            .buffer = (char *)response->payload,
            .capacity = response->payload_capacity,
            .length = 0U,
            .any = false,
        };
        size_t path_count = request->path_count == 0U ? 1U : request->path_count;
        int negotiated;
        uint16_t content_format;

        if (request->accepted_format_count > 0U) {
            size_t i;
            bool found = false;

            for (i = 0U; i < request->accepted_format_count; ++i) {
                if (request->accepted_formats[i] == SIRENA_LWM2M_CF_LINK_FORMAT) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                protocol_set_status(response, SIRENA_LWM2M_STATUS_NOT_ACCEPTABLE);
                return 0;
            }

            (void)i;
            negotiated = (int)SIRENA_LWM2M_CF_LINK_FORMAT;
        } else {
            negotiated = (int)SIRENA_LWM2M_CF_LINK_FORMAT;
        }

        content_format = (uint16_t)negotiated;

        if (path_count == 1U) {
            int rc2;

            rc2 = sirena_lwm2m_object_enabled(request->object_id) ? 0 : -ENOENT;
            if (rc2 != 0) {
                protocol_set_status(response, SIRENA_LWM2M_STATUS_NOT_FOUND);
                return 0;
            }

            rc = sirena_lwm2m_foreach_instance(discover_instance_visitor, &context);
            if (rc == -ENOMEM) {
                protocol_set_status(response, SIRENA_LWM2M_STATUS_INTERNAL_ERROR);
                return 0;
            }
            if (rc != 0 || !context.any) {
                protocol_set_status(response, SIRENA_LWM2M_STATUS_NOT_FOUND);
                return 0;
            }
        } else if (path_count == 2U) {
            struct sirena_lwm2m_instance *inst;

            rc = sirena_lwm2m_find_instance(request->object_id, request->instance_id, &inst);
            if (rc != 0) {
                protocol_set_status(response, SIRENA_LWM2M_STATUS_NOT_FOUND);
                return 0;
            }

            for (size_t i = 0U; i < inst->resource_count; ++i) {
                rc = discover_append_entry(&context, request->object_id, request->instance_id,
                                          inst->resources[i].id, true);
                if (rc != 0) {
                    protocol_set_status(response, SIRENA_LWM2M_STATUS_INTERNAL_ERROR);
                    return 0;
                }
            }
        } else if (path_count == 3U) {
            struct sirena_lwm2m_resource *res;

            rc = sirena_lwm2m_find_resource(request->object_id, request->instance_id,
                                            request->resource_id, &res);
            if (rc != 0) {
                protocol_set_status(response, SIRENA_LWM2M_STATUS_NOT_FOUND);
                return 0;
            }

            rc = discover_append_entry(&context, request->object_id, request->instance_id,
                                      request->resource_id, true);
            if (rc != 0) {
                protocol_set_status(response, SIRENA_LWM2M_STATUS_INTERNAL_ERROR);
                return 0;
            }
        } else {
            protocol_set_status(response, SIRENA_LWM2M_STATUS_NOT_FOUND);
            return 0;
        }

        if (context.length > 0U) {
            response->payload_length = context.length;
        } else {
            response->payload_length = 0U;
        }

        response->status = SIRENA_LWM2M_STATUS_CONTENT;
        response->content_format = content_format;
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

int sirena_lwm2m_protocol_send_encode(const struct sirena_lwm2m_send_record *records,
                                       size_t count, void *buffer, size_t *length)
{
    struct sirena_lwm2m_resource *resource;
    size_t i;
    size_t value_count = 0U;
    size_t encoded_length;
    int rc;

    if (records == NULL || count == 0U || buffer == NULL || length == NULL) {
        return -EINVAL;
    }

    if (count > CONFIG_SIRENA_LWM2M_CODEC_MAX_VALUES) {
        return -EINVAL;
    }

    for (i = 0U; i < count; ++i) {
        struct sirena_lwm2m_codec_value *value;
        size_t raw_length = CONFIG_SIRENA_LWM2M_PROTOCOL_VALUE_MAX;

        rc = sirena_lwm2m_find_resource(records[i].object_id, records[i].instance_id,
                                        records[i].resource_id, &resource);
        if (rc != 0) {
            return rc;
        }

        if ((resource->operations & SIRENA_LWM2M_OP_READ) == 0U || resource->read == NULL) {
            return -ENOTSUP;
        }

        rc = sirena_lwm2m_get(records[i].object_id, records[i].instance_id,
                              records[i].resource_id, protocol_send_raw[i], &raw_length);
        if (rc != 0) {
            return rc;
        }

        value = &protocol_send_values[i];
        rc = sirena_lwm2m_protocol_value_from_resource(resource, protocol_send_raw[i], raw_length, value);
        if (rc != 0) {
            return rc;
        }

        value->object_id = records[i].object_id;
        value->instance_id = records[i].instance_id;
        value->resource_id = records[i].resource_id;
        value->id = records[i].resource_id;
        value_count++;
    }

    encoded_length = *length;
    rc = sirena_lwm2m_codec_encode(SIRENA_LWM2M_CF_SENML_JSON, protocol_send_values,
                                     value_count, buffer, &encoded_length);
    if (rc != 0) {
        return rc;
    }

    *length = encoded_length;
    return 0;
}

int sirena_lwm2m_send(const struct sirena_lwm2m_send_record *records, size_t count)
{
    struct sirena_lwm2m_send_request request;

    if (records == NULL || count == 0U) {
        return -EINVAL;
    }

    request.records = records;
    request.count = count;

    return sirena_lwm2m_engine_submit(SIRENA_LWM2M_ENGINE_OPERATION_SEND, &request);
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
