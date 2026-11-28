#ifndef SIRENA_LWM2M_CODEC_H
#define SIRENA_LWM2M_CODEC_H

#include <sirena/lwm2m/object_registry.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SIRENA_LWM2M_CF_PLAIN_TEXT 0
#define SIRENA_LWM2M_CF_TLV 11542
#define SIRENA_LWM2M_CF_JSON 11543
#define SIRENA_LWM2M_CF_CBOR 11544
#define SIRENA_LWM2M_CF_SENML_JSON 110
#define SIRENA_LWM2M_CF_SENML_CBOR 112
#define SIRENA_LWM2M_CF_SENML_ETCH_JSON 11545
#define SIRENA_LWM2M_CF_SENML_ETCH_CBOR 11546
#define SIRENA_LWM2M_CF_OPAQUE 42
#define SIRENA_LWM2M_CF_LINK_FORMAT 40

#define SIRENA_LWM2M_COMPOSITE_PATH_MAX 48U
#define SIRENA_LWM2M_COMPOSITE_MAX_DEPTH 8U
#define SIRENA_LWM2M_COMPOSITE_VALUE_MAX (CONFIG_SIRENA_LWM2M_CODEC_MAX_VALUE_SIZE + 1U)

struct sirena_lwm2m_senml_record {
    char path[SIRENA_LWM2M_COMPOSITE_PATH_MAX];
    char value[SIRENA_LWM2M_COMPOSITE_VALUE_MAX];
    size_t value_length;
    bool value_is_string;
    bool value_invalid;
};

struct sirena_lwm2m_codec_value {
    uint16_t object_id;
    uint16_t instance_id;
    uint16_t resource_id;
    uint16_t id;
    enum sirena_lwm2m_value_type type;
    union {
        bool boolean;
        int64_t integer;
        uint64_t unsigned_integer;
        double floating_point;
        struct {
            const char *value;
            size_t length;
        } string;
        struct {
            const void *value;
            size_t length;
        } opaque;
        struct {
            uint16_t object_id;
            uint16_t instance_id;
        } object_link;
    } data;
};

bool sirena_lwm2m_content_format_supported(uint16_t format);
bool sirena_lwm2m_codec_can_encode(uint16_t format, enum sirena_lwm2m_value_type type);
int sirena_lwm2m_content_format_from_mime(const char *mime, uint16_t *format);
const char *sirena_lwm2m_content_format_to_mime(uint16_t format);
int sirena_lwm2m_content_format_negotiate(const uint16_t *accepted,
                                            size_t count,
                                            uint16_t preferred,
                                            enum sirena_lwm2m_value_type type);

int sirena_lwm2m_content_format_negotiate_multi(const uint16_t *accepted,
                                              size_t accepted_count,
                                              uint16_t preferred,
                                              const struct sirena_lwm2m_codec_value *values,
                                              size_t value_count,
                                              uint16_t *selected);

int sirena_lwm2m_codec_decode_senml_records(uint16_t content_format,
                                             const void *buffer,
                                             size_t length,
                                             struct sirena_lwm2m_senml_record *records,
                                             size_t *count);

int sirena_lwm2m_codec_encode(uint16_t content_format,
                              const struct sirena_lwm2m_codec_value *values,
                              size_t value_count, void *buffer,
                              size_t *length);

int sirena_lwm2m_codec_decode(uint16_t content_format,
                              enum sirena_lwm2m_value_type type,
                              const void *buffer,
                              size_t length,
                              struct sirena_lwm2m_codec_value *values,
                              size_t *value_count);

#endif