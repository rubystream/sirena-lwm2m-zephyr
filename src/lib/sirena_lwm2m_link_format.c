#include "sirena_lwm2m_link_format.h"
#include <sirena/lwm2m/object_registry.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <stdio.h>

#define SIRENA_LWM2M_SECURITY_OBJECT_ID 0U
#define SIRENA_LWM2M_SERVER_OBJECT_ID 1U
#define SIRENA_LWM2M_DEVICE_OBJECT_ID 3U

struct sirena_lwm2m_link_format_context {
    char *buffer;
    size_t capacity;
    size_t length;
    bool server_present;
    bool device_present;
};

static int link_format_append(uint16_t object_id, uint16_t instance_id, void *user_data)
{
    struct sirena_lwm2m_link_format_context *context = user_data;
    int written;

    if (object_id == SIRENA_LWM2M_SECURITY_OBJECT_ID) {
        return 0;
    }

    written = snprintf(context->buffer + context->length, context->capacity - context->length,
                       "%s</%u/%u>", context->length > 0U ? "," : "", object_id, instance_id);
    if (written < 0 || (size_t)written >= context->capacity - context->length) {
        return -ENOMEM;
    }

    context->length += (size_t)written;

    if (object_id == SIRENA_LWM2M_SERVER_OBJECT_ID) {
        context->server_present = true;
    } else if (object_id == SIRENA_LWM2M_DEVICE_OBJECT_ID) {
        context->device_present = true;
    }

    return 0;
}

int sirena_lwm2m_link_format_objects(char *buffer, size_t capacity, size_t *length)
{
    struct sirena_lwm2m_link_format_context context = {
        .buffer = buffer,
        .capacity = capacity,
        .length = 0U,
    };
    int rc;

    if (buffer == NULL || length == NULL || capacity == 0U) {
        return -EINVAL;
    }

    rc = sirena_lwm2m_foreach_instance(link_format_append, &context);
    if (rc != 0) {
        return rc;
    }

    if (!context.server_present || !context.device_present) {
        return -EINVAL;
    }

    *length = context.length;

    return 0;
}
