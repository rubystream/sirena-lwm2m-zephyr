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
    } data;
};

bool sirena_lwm2m_content_format_supported(uint16_t format);
int sirena_lwm2m_content_format_from_mime(const char *mime, uint16_t *format);
const char *sirena_lwm2m_content_format_to_mime(uint16_t format);
int sirena_lwm2m_content_format_negotiate(const uint16_t *accepted,
                                            size_t count,
                                            uint16_t preferred);

int sirena_lwm2m_codec_encode(uint16_t content_format,
                              const struct sirena_lwm2m_codec_value *values,
                              size_t value_count, void *buffer,
                              size_t *length);

int sirena_lwm2m_codec_decode(uint16_t content_format, const void *buffer,
                              size_t length,
                              struct sirena_lwm2m_codec_value *values,
                              size_t *value_count);

#endif