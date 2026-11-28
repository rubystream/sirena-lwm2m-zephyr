#ifndef SIRENA_LWM2M_PROTOCOL_H
#define SIRENA_LWM2M_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sirena/lwm2m/object_registry.h>
#include <sirena/lwm2m/codec.h>

enum sirena_lwm2m_status {
    SIRENA_LWM2M_STATUS_CHANGED = 0,
    SIRENA_LWM2M_STATUS_CREATED,
    SIRENA_LWM2M_STATUS_DELETED,
    SIRENA_LWM2M_STATUS_CONTENT,
    SIRENA_LWM2M_STATUS_BAD_REQUEST,
    SIRENA_LWM2M_STATUS_NOT_FOUND,
    SIRENA_LWM2M_STATUS_METHOD_NOT_ALLOWED,
    SIRENA_LWM2M_STATUS_NOT_ACCEPTABLE,
    SIRENA_LWM2M_STATUS_UNSUPPORTED_CONTENT_FORMAT,
    SIRENA_LWM2M_STATUS_INTERNAL_ERROR,
};

enum sirena_lwm2m_operation {
    SIRENA_LWM2M_OPERATION_READ = 0,
    SIRENA_LWM2M_OPERATION_WRITE,
    SIRENA_LWM2M_OPERATION_EXECUTE,
    SIRENA_LWM2M_OPERATION_CREATE,
    SIRENA_LWM2M_OPERATION_DELETE,
    SIRENA_LWM2M_OPERATION_WRITE_ATTRIBUTES,
    SIRENA_LWM2M_OPERATION_DISCOVER,
    SIRENA_LWM2M_OPERATION_WRITE_COMPOSITE,
    SIRENA_LWM2M_OPERATION_READ_COMPOSITE,
};

struct sirena_lwm2m_protocol_request {
    enum sirena_lwm2m_operation operation;
    uint16_t object_id;
    uint16_t instance_id;
    uint16_t resource_id;
    size_t path_count;
    struct sirena_lwm2m_resource_attributes attributes;
    const void *payload;
    size_t payload_length;
    bool content_format_present;
    uint16_t content_format;
    const uint16_t *accepted_formats;
    size_t accepted_format_count;
};

struct sirena_lwm2m_protocol_response {
    enum sirena_lwm2m_status status;
    uint16_t content_format;
    void *payload;
    size_t payload_capacity;
    size_t payload_length;
};

struct sirena_lwm2m_notify_request {
    uint16_t object_id;
    uint16_t instance_id;
    uint16_t resource_id;
};

struct sirena_lwm2m_send_record {
    uint16_t object_id;
    uint16_t instance_id;
    uint16_t resource_id;
};

struct sirena_lwm2m_send_request {
    const struct sirena_lwm2m_send_record *records;
    size_t count;
};


int sirena_lwm2m_protocol_init(void);
int sirena_lwm2m_protocol_execute(const struct sirena_lwm2m_protocol_request *request,
                                  struct sirena_lwm2m_protocol_response *response);
int sirena_lwm2m_protocol_read(uint16_t object_id, uint16_t instance_id, uint16_t resource_id,
                               uint16_t content_format, void *buffer, size_t *length);
int sirena_lwm2m_protocol_send_encode(const struct sirena_lwm2m_send_record *records,
                                       size_t count, void *buffer, size_t *length);
int sirena_lwm2m_send(const struct sirena_lwm2m_send_record *records, size_t count);

int sirena_lwm2m_protocol_value_from_resource(const struct sirena_lwm2m_resource *resource,
                                        const void *raw, size_t raw_length,
                                        struct sirena_lwm2m_codec_value *value);

#endif
