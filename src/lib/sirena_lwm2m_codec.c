#include <sirena/lwm2m/codec.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <inttypes.h>

static int append(void *buffer, size_t capacity, size_t *used,
                  const void *data, size_t data_len)
{
    if (*used + data_len > capacity) {
        return -ENOMEM;
    }
    memcpy((uint8_t *)buffer + *used, data, data_len);
    *used += data_len;
    return 0;
}

static int append_byte(void *buffer, size_t capacity, size_t *used, uint8_t b)
{
    return append(buffer, capacity, used, &b, 1U);
}

static const uint16_t codec_formats[] = {
    SIRENA_LWM2M_CF_TLV,
    SIRENA_LWM2M_CF_JSON,
    SIRENA_LWM2M_CF_CBOR,
    SIRENA_LWM2M_CF_SENML_JSON,
    SIRENA_LWM2M_CF_SENML_CBOR,
    SIRENA_LWM2M_CF_SENML_ETCH_JSON,
    SIRENA_LWM2M_CF_SENML_ETCH_CBOR,
    SIRENA_LWM2M_CF_PLAIN_TEXT,
    SIRENA_LWM2M_CF_OPAQUE,
};

static bool supported(uint16_t format)
{
    size_t i;

    for (i = 0U; i < ARRAY_SIZE(codec_formats); ++i) {
        if (codec_formats[i] == format) {
            return true;
        }
    }

    return false;
}

static bool encodable_type(enum sirena_lwm2m_value_type type)
{
    switch (type) {
    case SIRENA_LWM2M_VALUE_BOOLEAN:
    case SIRENA_LWM2M_VALUE_INTEGER:
    case SIRENA_LWM2M_VALUE_UNSIGNED:
    case SIRENA_LWM2M_VALUE_FLOAT:
    case SIRENA_LWM2M_VALUE_STRING:
    case SIRENA_LWM2M_VALUE_OPAQUE:
        return true;
    default:
        return false;
    }
}

bool sirena_lwm2m_codec_can_encode(uint16_t content_format, enum sirena_lwm2m_value_type type)
{
    if (!supported(content_format)) {
        return false;
    }

    if (content_format == SIRENA_LWM2M_CF_PLAIN_TEXT) {
        return type == SIRENA_LWM2M_VALUE_STRING;
    }

    if (content_format == SIRENA_LWM2M_CF_OPAQUE) {
        return type == SIRENA_LWM2M_VALUE_OPAQUE;
    }

    return encodable_type(type);
}

bool sirena_lwm2m_content_format_supported(uint16_t format)
{
    return supported(format);
}

int sirena_lwm2m_content_format_from_mime(const char *mime, uint16_t *format)
{
    char base[128];
    size_t i;
    if (mime == NULL || format == NULL) {
        return -EINVAL;
    }
    for (i = 0U; i < sizeof(base) - 1U && mime[i] != '\0' && mime[i] != ';'; ++i) {
        base[i] = mime[i];
    }
    base[i] = '\0';
    while (i > 0U && (base[i-1U] == ' ' || base[i-1U] == '\t')) {
        base[--i] = '\0';
    }
    const char *p = base;
    while (*p == ' ' || *p == '\t') ++p;
    if (strcmp(p, "text/plain") == 0) {
        *format = SIRENA_LWM2M_CF_PLAIN_TEXT;
        return 0;
    }
    if (strcmp(p, "application/vnd.oma.lwm2m+tlv") == 0) {
        *format = SIRENA_LWM2M_CF_TLV;
        return 0;
    }
    if (strcmp(p, "application/vnd.oma.lwm2m+json") == 0) {
        *format = SIRENA_LWM2M_CF_JSON;
        return 0;
    }
    if (strcmp(p, "application/vnd.oma.lwm2m+cbor") == 0) {
        *format = SIRENA_LWM2M_CF_CBOR;
        return 0;
    }
    if (strcmp(p, "application/senml+json") == 0) {
        *format = SIRENA_LWM2M_CF_SENML_JSON;
        return 0;
    }
    if (strcmp(p, "application/senml+cbor") == 0) {
        *format = SIRENA_LWM2M_CF_SENML_CBOR;
        return 0;
    }
    if (strcmp(p, "application/senml-etch+json") == 0) {
        *format = SIRENA_LWM2M_CF_SENML_ETCH_JSON;
        return 0;
    }
    if (strcmp(p, "application/senml-etch+cbor") == 0) {
        *format = SIRENA_LWM2M_CF_SENML_ETCH_CBOR;
        return 0;
    }
    if (strcmp(p, "application/octet-stream") == 0) {
        *format = SIRENA_LWM2M_CF_OPAQUE;
        return 0;
    }
    return -ENOTSUP;
}

const char *sirena_lwm2m_content_format_to_mime(uint16_t format)
{
    switch (format) {
    case SIRENA_LWM2M_CF_PLAIN_TEXT:
        return "text/plain";
    case SIRENA_LWM2M_CF_TLV:
        return "application/vnd.oma.lwm2m+tlv";
    case SIRENA_LWM2M_CF_JSON:
        return "application/vnd.oma.lwm2m+json";
    case SIRENA_LWM2M_CF_CBOR:
        return "application/vnd.oma.lwm2m+cbor";
    case SIRENA_LWM2M_CF_SENML_JSON:
        return "application/senml+json";
    case SIRENA_LWM2M_CF_SENML_CBOR:
        return "application/senml+cbor";
    case SIRENA_LWM2M_CF_SENML_ETCH_JSON:
        return "application/senml-etch+json";
    case SIRENA_LWM2M_CF_SENML_ETCH_CBOR:
        return "application/senml-etch+cbor";
    case SIRENA_LWM2M_CF_OPAQUE:
        return "application/octet-stream";
    default:
        return NULL;
    }
}

int sirena_lwm2m_content_format_negotiate(const uint16_t *accepted,
                                            size_t count,
                                            uint16_t preferred,
                                            enum sirena_lwm2m_value_type type)
{
    size_t i;

    if (accepted != NULL && count > 0U) {
        for (i = 0U; i < count; ++i) {
            if (sirena_lwm2m_codec_can_encode(accepted[i], type)) {
                return (int)accepted[i];
            }
        }

        return -ENOTSUP;
    }

    if (sirena_lwm2m_codec_can_encode(preferred, type)) {
        return (int)preferred;
    }

    for (i = 0U; i < ARRAY_SIZE(codec_formats); ++i) {
        if (sirena_lwm2m_codec_can_encode(codec_formats[i], type)) {
            return (int)codec_formats[i];
        }
    }

    return -ENOTSUP;
}

static int encode_tlv_value(void *buffer, size_t capacity, size_t *used,
                           const struct sirena_lwm2m_codec_value *v)
{
    uint8_t type_byte = 0xC0;
    uint16_t id = v->id;
    size_t value_len = 0U;
    uint8_t tmp[8];
    int rc;

    if (id > 255U) {
        type_byte |= 0x20;
    }

    switch (v->type) {
    case SIRENA_LWM2M_VALUE_BOOLEAN:
        value_len = 1U;
        tmp[0] = v->data.boolean ? 1U : 0U;
        break;
    case SIRENA_LWM2M_VALUE_INTEGER:
        value_len = 8U;
        for (size_t i = 0U; i < 8U; ++i) {
            tmp[7U - i] = (uint8_t)(((uint64_t)v->data.integer >> (i * 8U)) & 0xFF);
        }
        break;
    case SIRENA_LWM2M_VALUE_UNSIGNED:
        value_len = 8U;
        for (size_t i = 0U; i < 8U; ++i) {
            tmp[7U - i] = (uint8_t)((v->data.unsigned_integer >> (i * 8U)) & 0xFF);
        }
        break;
    case SIRENA_LWM2M_VALUE_FLOAT:
        value_len = 8U;
        {
            uint64_t bits;
            memcpy(&bits, &v->data.floating_point, 8U);
            for (size_t i = 0U; i < 8U; ++i) {
                tmp[i] = (uint8_t)((bits >> ((7U - i) * 8U)) & 0xFF);
            }
        }
        break;
    case SIRENA_LWM2M_VALUE_STRING:
        value_len = v->data.string.length;
        break;
    case SIRENA_LWM2M_VALUE_OPAQUE:
        value_len = v->data.opaque.length;
        break;
    default:
        return -EINVAL;
    }

    if (value_len <= 7U) {
        type_byte |= (uint8_t)value_len;
    } else if (value_len <= 255U) {
        type_byte |= 0x08;
    } else if (value_len <= 65535U) {
        type_byte |= 0x10;
    } else {
        type_byte |= 0x18;
    }

    rc = append_byte(buffer, capacity, used, type_byte);
    if (rc != 0) {
        return rc;
    }
    if (id > 255U) {
        uint8_t hi[2] = { (uint8_t)(id >> 8), (uint8_t)(id & 0xFF) };
        rc = append(buffer, capacity, used, hi, 2U);
    } else {
        uint8_t lo = (uint8_t)id;
        rc = append(buffer, capacity, used, &lo, 1U);
    }
    if (rc != 0) {
        return rc;
    }
    if (value_len > 7U) {
        if (value_len <= 255U) {
            uint8_t l = (uint8_t)value_len;
            rc = append(buffer, capacity, used, &l, 1U);
        } else if (value_len <= 65535U) {
            uint8_t l[2] = { (uint8_t)(value_len >> 8), (uint8_t)(value_len & 0xFF) };
            rc = append(buffer, capacity, used, l, 2U);
        } else {
            uint8_t l[3] = { (uint8_t)((value_len >> 16) & 0xFF),
                             (uint8_t)((value_len >> 8) & 0xFF),
                             (uint8_t)(value_len & 0xFF) };
            rc = append(buffer, capacity, used, l, 3U);
        }
        if (rc != 0) {
            return rc;
        }
    }
    switch (v->type) {
    case SIRENA_LWM2M_VALUE_STRING:
        return append(buffer, capacity, used, v->data.string.value,
                      v->data.string.length);
    case SIRENA_LWM2M_VALUE_OPAQUE:
        return append(buffer, capacity, used, v->data.opaque.value,
                      v->data.opaque.length);
    default:
        return append(buffer, capacity, used, tmp, value_len);
    }
}

static int cbor_length(void *buffer, size_t capacity, size_t *used,
                       uint8_t major, size_t len)
{
    if (len <= 23U) {
        uint8_t b = (uint8_t)((major << 5) | len);
        return append_byte(buffer, capacity, used, b);
    }
    if (len <= 255U) {
        uint8_t b = (uint8_t)((major << 5) | 24);
        int rc = append_byte(buffer, capacity, used, b);
        if (rc != 0) return rc;
        uint8_t x = (uint8_t)len;
        return append_byte(buffer, capacity, used, x);
    }
    if (len <= 65535U) {
        uint8_t b = (uint8_t)((major << 5) | 25);
        int rc = append_byte(buffer, capacity, used, b);
        if (rc != 0) return rc;
        uint8_t x[2] = { (uint8_t)(len >> 8), (uint8_t)(len & 0xFF) };
        return append(buffer, capacity, used, x, 2U);
    }
    if (len <= 0xFFFFFFFFU) {
        uint8_t b = (uint8_t)((major << 5) | 26);
        int rc = append_byte(buffer, capacity, used, b);
        if (rc != 0) return rc;
        uint8_t x[4];
        for (size_t i = 0U; i < 4U; ++i) {
            x[3U - i] = (uint8_t)((len >> (i * 8U)) & 0xFF);
        }
        return append(buffer, capacity, used, x, 4U);
    }
    {
        uint8_t b = (uint8_t)((major << 5) | 27);
        int rc = append_byte(buffer, capacity, used, b);
        if (rc != 0) return rc;
        uint8_t x[8];
        for (size_t i = 0U; i < 8U; ++i) {
            x[7U - i] = (uint8_t)((len >> (i * 8U)) & 0xFF);
        }
        return append(buffer, capacity, used, x, 8U);
    }
}

static int encode_cbor_value(void *buffer, size_t capacity, size_t *used,
                            const struct sirena_lwm2m_codec_value *v)
{
    int rc;

    switch (v->type) {
    case SIRENA_LWM2M_VALUE_BOOLEAN:
        return append_byte(buffer, capacity, used,
                          v->data.boolean ? 0xF5 : 0xF4);
    case SIRENA_LWM2M_VALUE_INTEGER:
        if (v->data.integer >= 0) {
            return cbor_length(buffer, capacity, used, 0,
                              (size_t)v->data.integer);
        } else {
            size_t n = (size_t)(-(v->data.integer + 1));
            return cbor_length(buffer, capacity, used, 1, n);
        }
    case SIRENA_LWM2M_VALUE_UNSIGNED:
        return cbor_length(buffer, capacity, used, 0,
                          (size_t)v->data.unsigned_integer);
    case SIRENA_LWM2M_VALUE_FLOAT: {
        uint8_t head = 0xFB;
        uint64_t bits;
        uint8_t bytes[8];
        rc = append_byte(buffer, capacity, used, head);
        if (rc != 0) return rc;
        memcpy(&bits, &v->data.floating_point, 8U);
        for (size_t i = 0U; i < 8U; ++i) {
            bytes[i] = (uint8_t)((bits >> ((7U - i) * 8U)) & 0xFF);
        }
        return append(buffer, capacity, used, bytes, 8U);
    }
    case SIRENA_LWM2M_VALUE_STRING:
        rc = cbor_length(buffer, capacity, used, 3, v->data.string.length);
        if (rc != 0) return rc;
        return append(buffer, capacity, used, v->data.string.value,
                      v->data.string.length);
    case SIRENA_LWM2M_VALUE_OPAQUE:
        rc = cbor_length(buffer, capacity, used, 2, v->data.opaque.length);
        if (rc != 0) return rc;
        return append(buffer, capacity, used, v->data.opaque.value,
                      v->data.opaque.length);
    default:
        return -EINVAL;
    }
}

static int append_escaped(void *buffer, size_t capacity, size_t *used,
                          const char *value, size_t length)
{
    size_t i;
    if (append_byte(buffer, capacity, used, '"') != 0) return -ENOMEM;
    for (i = 0U; i < length; ++i) {
        char c = value[i];
        if (c == '"' || c == '\\') {
            if (append_byte(buffer, capacity, used, '\\') != 0) return -ENOMEM;
            if (append_byte(buffer, capacity, used, (uint8_t)c) != 0) return -ENOMEM;
        } else if (c == '\n') {
            if (append(buffer, capacity, used, "\\n", 2U) != 0) return -ENOMEM;
        } else if (c == '\r') {
            if (append(buffer, capacity, used, "\\r", 2U) != 0) return -ENOMEM;
        } else if (c == '\t') {
            if (append(buffer, capacity, used, "\\t", 2U) != 0) return -ENOMEM;
        } else {
            if (append_byte(buffer, capacity, used, (uint8_t)c) != 0) return -ENOMEM;
        }
    }
    return append_byte(buffer, capacity, used, '"');
}

static uint16_t effective_resource_id(const struct sirena_lwm2m_codec_value *v)
{
    return v->resource_id ? v->resource_id : v->id;
}

int sirena_lwm2m_codec_encode(uint16_t content_format,
                              const struct sirena_lwm2m_codec_value *values,
                              size_t value_count, void *buffer,
                              size_t *length)
{
    size_t used = 0U;
    size_t cap;
    int rc;
    size_t i;

    if (buffer == NULL || length == NULL) {
        return -EINVAL;
    }
    cap = *length;
    *length = 0U;
    if (value_count == 0U) {
        return -EINVAL;
    }
    if (values == NULL && value_count != 0U) {
        return -EINVAL;
    }
    if (value_count > CONFIG_SIRENA_LWM2M_CODEC_MAX_VALUES) {
        return -ENOMEM;
    }
    for (i = 0U; i < value_count; ++i) {
        size_t sz = 0U;
        if (values[i].type == SIRENA_LWM2M_VALUE_STRING) {
            sz = values[i].data.string.length;
        } else if (values[i].type == SIRENA_LWM2M_VALUE_OPAQUE) {
            sz = values[i].data.opaque.length;
        }
        if (sz > CONFIG_SIRENA_LWM2M_CODEC_MAX_VALUE_SIZE) {
            return -ENOMEM;
        }
    }
    if (!supported(content_format)) {
        return -ENOTSUP;
    }

    if (content_format == SIRENA_LWM2M_CF_PLAIN_TEXT) {
        if (value_count != 1U) return -EINVAL;
        if (values[0].type == SIRENA_LWM2M_VALUE_STRING) {
            rc = append(buffer, cap, &used, values[0].data.string.value,
                        values[0].data.string.length);
        } else {
            rc = -EINVAL;
        }
        if (rc != 0) return rc;
        *length = used;
        return 0;
    }

    if (content_format == SIRENA_LWM2M_CF_OPAQUE) {
        if (value_count != 1U) return -EINVAL;
        if (values[0].type == SIRENA_LWM2M_VALUE_OPAQUE) {
            rc = append(buffer, cap, &used, values[0].data.opaque.value,
                        values[0].data.opaque.length);
        } else {
            rc = -EINVAL;
        }
        if (rc != 0) return rc;
        *length = used;
        return 0;
    }

    if (content_format == SIRENA_LWM2M_CF_TLV) {
        for (i = 0U; i < value_count; ++i) {
            rc = encode_tlv_value(buffer, cap, &used, &values[i]);
            if (rc != 0) return rc;
        }
        *length = used;
        return 0;
    }

    if (content_format == SIRENA_LWM2M_CF_CBOR) {
        if (value_count == 1U) {
            rc = encode_cbor_value(buffer, cap, &used, &values[0]);
        } else {
            uint8_t arr = 0x80 | (uint8_t)(value_count <= 23U ? value_count : 23U);
            if (value_count > 23U) return -ENOTSUP;
            rc = append_byte(buffer, cap, &used, arr);
            if (rc != 0) return rc;
            for (i = 0U; i < value_count; ++i) {
                rc = encode_cbor_value(buffer, cap, &used, &values[i]);
                if (rc != 0) return rc;
            }
        }
        *length = used;
        return 0;
    }

    if (content_format == SIRENA_LWM2M_CF_SENML_CBOR) {
        char base[64];
        snprintf(base, sizeof(base), "/%u/%u/", values[0].object_id, values[0].instance_id);
        uint8_t map = 0xA0 | 2U;
        rc = append_byte(buffer, cap, &used, map);
        if (rc != 0) return rc;
        rc = cbor_length(buffer, cap, &used, 3, 2);
        if (rc != 0) return rc;
        rc = append(buffer, cap, &used, "bn", 2U);
        if (rc != 0) return rc;
        rc = cbor_length(buffer, cap, &used, 3, strlen(base));
        if (rc != 0) return rc;
        rc = append(buffer, cap, &used, base, strlen(base));
        if (rc != 0) return rc;
        rc = cbor_length(buffer, cap, &used, 3, 1);
        if (rc != 0) return rc;
        rc = append(buffer, cap, &used, "e", 1U);
        if (rc != 0) return rc;
        uint8_t arr = 0x80 | (uint8_t)(value_count <= 23U ? value_count : 23U);
        if (value_count > 23U) return -ENOTSUP;
        rc = append_byte(buffer, cap, &used, arr);
        if (rc != 0) return rc;
        for (i = 0U; i < value_count; ++i) {
            uint8_t rec = 0xA0 | 2U;
            char rid[16];
            snprintf(rid, sizeof(rid), "%u", effective_resource_id(&values[i]));
            rc = append_byte(buffer, cap, &used, rec);
            if (rc != 0) return rc;
            rc = cbor_length(buffer, cap, &used, 3, 1);
            if (rc != 0) return rc;
            rc = append(buffer, cap, &used, "n", 1U);
            if (rc != 0) return rc;
            rc = cbor_length(buffer, cap, &used, 3, strlen(rid));
            if (rc != 0) return rc;
            rc = append(buffer, cap, &used, rid, strlen(rid));
            if (rc != 0) return rc;
            rc = cbor_length(buffer, cap, &used, 3, 1);
            if (rc != 0) return rc;
            rc = append(buffer, cap, &used, "v", 1U);
            if (rc != 0) return rc;
            rc = encode_cbor_value(buffer, cap, &used, &values[i]);
            if (rc != 0) return rc;
        }
        *length = used;
        return 0;
    }

    if (content_format == SIRENA_LWM2M_CF_SENML_ETCH_CBOR) {
        uint8_t arr = 0x80 | (uint8_t)(value_count <= 23U ? value_count : 23U);
        if (value_count > 23U) return -ENOTSUP;
        rc = append_byte(buffer, cap, &used, arr);
        if (rc != 0) return rc;
        for (i = 0U; i < value_count; ++i) {
            char path[64];
            snprintf(path, sizeof(path), "/%u/%u/%u", values[i].object_id,
                     values[i].instance_id,
                     effective_resource_id(&values[i]));
            uint8_t rec = 0xA0 | 3U;
            rc = append_byte(buffer, cap, &used, rec);
            if (rc != 0) return rc;
            rc = cbor_length(buffer, cap, &used, 3, 1);
            if (rc != 0) return rc;
            rc = append(buffer, cap, &used, "v", 1U);
            if (rc != 0) return rc;
            rc = encode_cbor_value(buffer, cap, &used, &values[i]);
            if (rc != 0) return rc;
            rc = cbor_length(buffer, cap, &used, 3, 1);
            if (rc != 0) return rc;
            rc = append(buffer, cap, &used, "n", 1U);
            if (rc != 0) return rc;
            rc = cbor_length(buffer, cap, &used, 3, strlen(path));
            if (rc != 0) return rc;
            rc = append(buffer, cap, &used, path, strlen(path));
            if (rc != 0) return rc;
            rc = cbor_length(buffer, cap, &used, 3, 2);
            if (rc != 0) return rc;
            rc = append(buffer, cap, &used, "vb", 2U);
            if (rc != 0) return rc;
            rc = append_byte(buffer, cap, &used, 0xF5);
            if (rc != 0) return rc;
        }
        *length = used;
        return 0;
    }

    if (content_format == SIRENA_LWM2M_CF_JSON) {
        if (append_byte(buffer, cap, &used, '{') != 0) return -ENOMEM;
        for (i = 0U; i < value_count; ++i) {
            char tmp[128];
            int n;
            if (i != 0U) {
                if (append_byte(buffer, cap, &used, ',') != 0) return -ENOMEM;
            }
            n = snprintf(tmp, sizeof(tmp), "\"%u\":", effective_resource_id(&values[i]));
            if (n < 0 || append(buffer, cap, &used, tmp, (size_t)n) != 0) return -ENOMEM;
            switch (values[i].type) {
            case SIRENA_LWM2M_VALUE_BOOLEAN: {
                const char *str = values[i].data.boolean ? "true" : "false";
                if (append(buffer, cap, &used, str, strlen(str)) != 0) return -ENOMEM;
                break;
            }
            case SIRENA_LWM2M_VALUE_INTEGER:
                n = snprintf(tmp, sizeof(tmp), "%" PRId64, values[i].data.integer);
                if (n < 0 || append(buffer, cap, &used, tmp, (size_t)n) != 0) return -ENOMEM;
                break;
            case SIRENA_LWM2M_VALUE_UNSIGNED:
                n = snprintf(tmp, sizeof(tmp), "%" PRIu64, values[i].data.unsigned_integer);
                if (n < 0 || append(buffer, cap, &used, tmp, (size_t)n) != 0) return -ENOMEM;
                break;
            case SIRENA_LWM2M_VALUE_FLOAT:
                n = snprintf(tmp, sizeof(tmp), "%g", values[i].data.floating_point);
                if (n < 0 || append(buffer, cap, &used, tmp, (size_t)n) != 0) return -ENOMEM;
                break;
            case SIRENA_LWM2M_VALUE_STRING:
                if (append_escaped(buffer, cap, &used, values[i].data.string.value, values[i].data.string.length) != 0) return -ENOMEM;
                break;
            case SIRENA_LWM2M_VALUE_OPAQUE:
                if (append_escaped(buffer, cap, &used, (const char *)values[i].data.opaque.value, values[i].data.opaque.length) != 0) return -ENOMEM;
                break;
            default:
                return -EINVAL;
            }
        }
        if (append_byte(buffer, cap, &used, '}') != 0) return -ENOMEM;
        *length = used;
        return 0;
    }

    if (content_format == SIRENA_LWM2M_CF_SENML_JSON) {
        char bn[64];
        snprintf(bn, sizeof(bn), "{\"bn\":\"/%u/%u/\",\"e\":[", values[0].object_id, values[0].instance_id);
        if (append(buffer, cap, &used, bn, strlen(bn)) != 0) return -ENOMEM;
        for (i = 0U; i < value_count; ++i) {
            char tmp[128];
            int n;
            if (i != 0U) {
                if (append_byte(buffer, cap, &used, ',') != 0) return -ENOMEM;
            }
            n = snprintf(tmp, sizeof(tmp), "{\"n\":\"%u\",\"v\":", effective_resource_id(&values[i]));
            if (n < 0 || append(buffer, cap, &used, tmp, (size_t)n) != 0) return -ENOMEM;
            switch (values[i].type) {
            case SIRENA_LWM2M_VALUE_BOOLEAN: {
                const char *str = values[i].data.boolean ? "true" : "false";
                if (append(buffer, cap, &used, str, strlen(str)) != 0) return -ENOMEM;
                break;
            }
            case SIRENA_LWM2M_VALUE_INTEGER:
                n = snprintf(tmp, sizeof(tmp), "%" PRId64, values[i].data.integer);
                if (n < 0 || append(buffer, cap, &used, tmp, (size_t)n) != 0) return -ENOMEM;
                break;
            case SIRENA_LWM2M_VALUE_UNSIGNED:
                n = snprintf(tmp, sizeof(tmp), "%" PRIu64, values[i].data.unsigned_integer);
                if (n < 0 || append(buffer, cap, &used, tmp, (size_t)n) != 0) return -ENOMEM;
                break;
            case SIRENA_LWM2M_VALUE_FLOAT:
                n = snprintf(tmp, sizeof(tmp), "%g", values[i].data.floating_point);
                if (n < 0 || append(buffer, cap, &used, tmp, (size_t)n) != 0) return -ENOMEM;
                break;
            case SIRENA_LWM2M_VALUE_STRING:
                if (append_escaped(buffer, cap, &used, values[i].data.string.value, values[i].data.string.length) != 0) return -ENOMEM;
                break;
            case SIRENA_LWM2M_VALUE_OPAQUE:
                if (append_escaped(buffer, cap, &used, (const char *)values[i].data.opaque.value, values[i].data.opaque.length) != 0) return -ENOMEM;
                break;
            default:
                return -EINVAL;
            }
            if (append_byte(buffer, cap, &used, '}') != 0) return -ENOMEM;
        }
        if (append(buffer, cap, &used, "]}", 2U) != 0) return -ENOMEM;
        *length = used;
        return 0;
    }

    if (content_format == SIRENA_LWM2M_CF_SENML_ETCH_JSON) {
        if (append(buffer, cap, &used, "{\"e\":[", 6U) != 0) return -ENOMEM;
        for (i = 0U; i < value_count; ++i) {
            char path[64];
            char tmp[128];
            int n;
            if (i != 0U) {
                if (append_byte(buffer, cap, &used, ',') != 0) return -ENOMEM;
            }
            snprintf(path, sizeof(path), "/%u/%u/%u", values[i].object_id, values[i].instance_id, effective_resource_id(&values[i]));
            n = snprintf(tmp, sizeof(tmp), "{\"n\":\"%s\",\"v\":", path);
            if (n < 0 || append(buffer, cap, &used, tmp, (size_t)n) != 0) return -ENOMEM;
            switch (values[i].type) {
            case SIRENA_LWM2M_VALUE_BOOLEAN: {
                const char *str = values[i].data.boolean ? "true" : "false";
                if (append(buffer, cap, &used, str, strlen(str)) != 0) return -ENOMEM;
                break;
            }
            case SIRENA_LWM2M_VALUE_INTEGER:
                n = snprintf(tmp, sizeof(tmp), "%" PRId64, values[i].data.integer);
                if (n < 0 || append(buffer, cap, &used, tmp, (size_t)n) != 0) return -ENOMEM;
                break;
            case SIRENA_LWM2M_VALUE_UNSIGNED:
                n = snprintf(tmp, sizeof(tmp), "%" PRIu64, values[i].data.unsigned_integer);
                if (n < 0 || append(buffer, cap, &used, tmp, (size_t)n) != 0) return -ENOMEM;
                break;
            case SIRENA_LWM2M_VALUE_FLOAT:
                n = snprintf(tmp, sizeof(tmp), "%g", values[i].data.floating_point);
                if (n < 0 || append(buffer, cap, &used, tmp, (size_t)n) != 0) return -ENOMEM;
                break;
            case SIRENA_LWM2M_VALUE_STRING:
                if (append_escaped(buffer, cap, &used, values[i].data.string.value, values[i].data.string.length) != 0) return -ENOMEM;
                break;
            case SIRENA_LWM2M_VALUE_OPAQUE:
                if (append_escaped(buffer, cap, &used, (const char *)values[i].data.opaque.value, values[i].data.opaque.length) != 0) return -ENOMEM;
                break;
            default:
                return -EINVAL;
            }
            if (append(buffer, cap, &used, ",\"vb\":true}", 11U) != 0) return -ENOMEM;
        }
        if (append(buffer, cap, &used, "]}", 2U) != 0) return -ENOMEM;
        *length = used;
        return 0;
    }

    return -ENOTSUP;
}

static bool decode_signed_integer(const uint8_t *bytes, size_t length, int64_t *value)
{
    int64_t result;

    if (length != 1U && length != 2U && length != 4U && length != 8U) {
        return false;
    }

    result = (bytes[0] & 0x80U) ? -1 : 0;

    for (size_t i = 0U; i < length; ++i) {
        result = (int64_t)(((uint64_t)result << 8) | bytes[i]);
    }

    *value = result;

    return true;
}

static bool decode_unsigned_integer(const uint8_t *bytes, size_t length, uint64_t *value)
{
    uint64_t result = 0U;

    if (length != 1U && length != 2U && length != 4U && length != 8U) {
        return false;
    }

    for (size_t i = 0U; i < length; ++i) {
        result = (result << 8) | bytes[i];
    }

    *value = result;

    return true;
}

static int decode_tlv_value(const uint8_t *buf, size_t len,
                           struct sirena_lwm2m_codec_value *v,
                           size_t *consumed,
                           enum sirena_lwm2m_value_type expected)
{
    uint8_t type_byte;
    size_t cursor = 0U;
    size_t id_bytes;
    uint16_t id;
    uint8_t length_type;
    size_t value_len;
    size_t value_start;
    if (len == 0U) return -EINVAL;
    type_byte = buf[0];
    cursor = 1U;
    id_bytes = (type_byte & 0x20) ? 2U : 1U;
    if (cursor + id_bytes > len) return -EINVAL;
    if (id_bytes == 2U) {
        id = ((uint16_t)buf[cursor] << 8) | buf[cursor + 1];
    } else {
        id = buf[cursor];
    }
    cursor += id_bytes;
    length_type = (type_byte & 0x18) >> 3;
    if (length_type == 0U) {
        value_len = type_byte & 0x07;
    } else if (length_type == 1U) {
        if (cursor + 1U > len) return -EINVAL;
        value_len = buf[cursor];
        cursor += 1U;
    } else if (length_type == 2U) {
        if (cursor + 2U > len) return -EINVAL;
        value_len = ((size_t)buf[cursor] << 8) | buf[cursor + 1];
        cursor += 2U;
    } else {
        if (cursor + 3U > len) return -EINVAL;
        value_len = ((size_t)buf[cursor] << 16) |
                    ((size_t)buf[cursor + 1] << 8) | buf[cursor + 2];
        cursor += 3U;
    }
    if (cursor + value_len > len) return -EINVAL;
    value_start = cursor;
    *consumed = value_start + value_len;
    v->object_id = 0U;
    v->instance_id = 0U;
    v->resource_id = id;
    v->id = id;
    switch (expected) {
    case SIRENA_LWM2M_VALUE_BOOLEAN:
        if (value_len != 1U) return -EINVAL;
        v->type = SIRENA_LWM2M_VALUE_BOOLEAN;
        v->data.boolean = buf[value_start] != 0U;
        break;
    case SIRENA_LWM2M_VALUE_INTEGER: {
        int64_t value = 0;

        if (!decode_signed_integer(&buf[value_start], value_len, &value)) return -EINVAL;
        v->type = SIRENA_LWM2M_VALUE_INTEGER;
        v->data.integer = value;
        break;
    }
    case SIRENA_LWM2M_VALUE_UNSIGNED: {
        uint64_t value = 0U;

        if (!decode_unsigned_integer(&buf[value_start], value_len, &value)) return -EINVAL;
        v->type = SIRENA_LWM2M_VALUE_UNSIGNED;
        v->data.unsigned_integer = value;
        break;
    }
    case SIRENA_LWM2M_VALUE_FLOAT: {
        if (value_len == 4U) {
            uint32_t bits = 0U;
            float single;

            for (size_t i = 0U; i < 4U; ++i) {
                bits = (bits << 8) | buf[value_start + i];
            }
            memcpy(&single, &bits, sizeof(single));
            v->data.floating_point = (double)single;
        } else if (value_len == 8U) {
            uint64_t bits = 0U;

            for (size_t i = 0U; i < 8U; ++i) {
                bits = (bits << 8) | buf[value_start + i];
            }
            memcpy(&v->data.floating_point, &bits, sizeof(v->data.floating_point));
        } else {
            return -EINVAL;
        }
        v->type = SIRENA_LWM2M_VALUE_FLOAT;
        break;
    }
    case SIRENA_LWM2M_VALUE_STRING:
        v->type = SIRENA_LWM2M_VALUE_STRING;
        v->data.string.value = (const char *)&buf[value_start];
        v->data.string.length = value_len;
        break;
    default:
        v->type = SIRENA_LWM2M_VALUE_OPAQUE;
        v->data.opaque.value = &buf[value_start];
        v->data.opaque.length = value_len;
        break;
    }
    return 0;
}

int sirena_lwm2m_codec_decode(uint16_t content_format,
                              enum sirena_lwm2m_value_type type,
                              const void *buffer,
                              size_t length,
                              struct sirena_lwm2m_codec_value *values,
                              size_t *value_count)
{
    size_t cap;

    if (buffer == NULL || value_count == NULL) {
        return -EINVAL;
    }
    cap = *value_count;
    *value_count = 0U;
    if (values == NULL && cap != 0U) {
        return -EINVAL;
    }
    if (cap > CONFIG_SIRENA_LWM2M_CODEC_MAX_VALUES) {
        return -ENOMEM;
    }
    if (!supported(content_format)) {
        return -ENOTSUP;
    }

    if (content_format == SIRENA_LWM2M_CF_PLAIN_TEXT) {
        if (cap < 1U) return -ENOMEM;
        values[0].object_id = 0U;
        values[0].instance_id = 0U;
        values[0].resource_id = 0U;
        values[0].id = 0U;
        values[0].type = SIRENA_LWM2M_VALUE_STRING;
        values[0].data.string.value = buffer;
        values[0].data.string.length = length;
        *value_count = 1U;
        return 0;
    }

    if (content_format == SIRENA_LWM2M_CF_OPAQUE) {
        if (cap < 1U) return -ENOMEM;
        values[0].object_id = 0U;
        values[0].instance_id = 0U;
        values[0].resource_id = 0U;
        values[0].id = 0U;
        values[0].type = SIRENA_LWM2M_VALUE_OPAQUE;
        values[0].data.opaque.value = buffer;
        values[0].data.opaque.length = length;
        *value_count = 1U;
        return 0;
    }

    if (content_format == SIRENA_LWM2M_CF_TLV) {
        size_t cursor = 0U;
        size_t count = 0U;
        while (cursor < length) {
            size_t consumed = 0U;
            if (count >= cap) return -ENOMEM;
            if (decode_tlv_value((const uint8_t *)buffer + cursor,
                                length - cursor, &values[count],
                                &consumed, type) != 0) {
                return -EINVAL;
            }
            count += 1U;
            cursor += consumed;
        }
        *value_count = count;
        return 0;
    }

    (void)buffer;
    (void)length;
    (void)values;
    if (content_format == SIRENA_LWM2M_CF_CBOR ||
        content_format == SIRENA_LWM2M_CF_SENML_CBOR ||
        content_format == SIRENA_LWM2M_CF_SENML_ETCH_CBOR ||
        content_format == SIRENA_LWM2M_CF_JSON ||
        content_format == SIRENA_LWM2M_CF_SENML_JSON ||
        content_format == SIRENA_LWM2M_CF_SENML_ETCH_JSON) {
        return -ENOTSUP;
    }

    return -ENOTSUP;
}
