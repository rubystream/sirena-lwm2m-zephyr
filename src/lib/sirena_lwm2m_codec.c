#include <sirena/lwm2m/codec.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>

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
    case SIRENA_LWM2M_VALUE_OBJECT_LINK:
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

int sirena_lwm2m_content_format_negotiate_multi(const uint16_t *accepted,
                                                size_t accepted_count,
                                                uint16_t preferred,
                                                const struct sirena_lwm2m_codec_value *values,
                                                size_t value_count, uint16_t *selected)
{
    size_t vi;
    size_t candidate;

    if (selected == NULL || values == NULL || value_count == 0U) {
        return -EINVAL;
    }

    *selected = 0U;

    if (accepted != NULL && accepted_count > 0U) {
        for (candidate = 0U; candidate < accepted_count; ++candidate) {
            bool ok = true;

            if (accepted[candidate] != SIRENA_LWM2M_CF_SENML_JSON &&
                accepted[candidate] != SIRENA_LWM2M_CF_SENML_CBOR) {
                continue;
            }
            for (vi = 0U; vi < value_count; ++vi) {
                if (!sirena_lwm2m_codec_can_encode(accepted[candidate], values[vi].type)) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                *selected = accepted[candidate];
                return 0;
            }
        }
        return -ENOTSUP;
    }

    {
        uint16_t pref = preferred;

        if (pref != SIRENA_LWM2M_CF_SENML_JSON && pref != SIRENA_LWM2M_CF_SENML_CBOR) {
            pref = SIRENA_LWM2M_CF_SENML_JSON;
        }
        {
            bool ok = true;
            for (vi = 0U; vi < value_count; ++vi) {
                if (!sirena_lwm2m_codec_can_encode(pref, values[vi].type)) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                *selected = pref;
                return 0;
            }
        }
    }

    {
        const uint16_t formats[] = { SIRENA_LWM2M_CF_SENML_JSON, SIRENA_LWM2M_CF_SENML_CBOR };

        for (candidate = 0U; candidate < ARRAY_SIZE(formats); ++candidate) {
            bool ok = true;
            for (vi = 0U; vi < value_count; ++vi) {
                if (!sirena_lwm2m_codec_can_encode(formats[candidate], values[vi].type)) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                *selected = formats[candidate];
                return 0;
            }
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
    case SIRENA_LWM2M_VALUE_OBJECT_LINK:
        value_len = 4U;
        tmp[0] = (uint8_t)(v->data.object_link.object_id >> 8);
        tmp[1] = (uint8_t)(v->data.object_link.object_id & 0xFF);
        tmp[2] = (uint8_t)(v->data.object_link.instance_id >> 8);
        tmp[3] = (uint8_t)(v->data.object_link.instance_id & 0xFF);
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
    case SIRENA_LWM2M_VALUE_OBJECT_LINK: {
        uint8_t bytes[4] = {
            (uint8_t)(v->data.object_link.object_id >> 8),
            (uint8_t)(v->data.object_link.object_id & 0xFF),
            (uint8_t)(v->data.object_link.instance_id >> 8),
            (uint8_t)(v->data.object_link.instance_id & 0xFF),
        };
        rc = cbor_length(buffer, capacity, used, 2, 4U);
        if (rc != 0) return rc;
        return append(buffer, capacity, used, bytes, 4U);
    }
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
        bool same_base = true;
        size_t si;
        for (si = 1U; si < value_count; ++si) {
            if (values[si].object_id != values[0].object_id ||
                values[si].instance_id != values[0].instance_id) {
                same_base = false;
                break;
            }
        }
        if (same_base) {
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
        } else {
            uint8_t map = 0xA0 | 1U;
            rc = append_byte(buffer, cap, &used, map);
            if (rc != 0) return rc;
            rc = cbor_length(buffer, cap, &used, 3, 1);
            if (rc != 0) return rc;
            rc = append(buffer, cap, &used, "e", 1U);
            if (rc != 0) return rc;
        }
        uint8_t arr = 0x80 | (uint8_t)(value_count <= 23U ? value_count : 23U);
        if (value_count > 23U) return -ENOTSUP;
        rc = append_byte(buffer, cap, &used, arr);
        if (rc != 0) return rc;
        for (i = 0U; i < value_count; ++i) {
            uint8_t rec = 0xA0 | 2U;
            char rid[64];
            if (values[i].object_id != values[0].object_id || values[i].instance_id != values[0].instance_id || !same_base) {
                snprintf(rid, sizeof(rid), "/%u/%u/%u", values[i].object_id, values[i].instance_id,
                         effective_resource_id(&values[i]));
            } else {
                snprintf(rid, sizeof(rid), "%u", effective_resource_id(&values[i]));
            }
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
            case SIRENA_LWM2M_VALUE_OBJECT_LINK: {
                char ol[32];
                int ol_n = snprintf(ol, sizeof(ol), "%u:%u",
                                    values[i].data.object_link.object_id,
                                    values[i].data.object_link.instance_id);
                if (ol_n < 0) return -ENOMEM;
                if (append_byte(buffer, cap, &used, '"') != 0) return -ENOMEM;
                if (append(buffer, cap, &used, ol, (size_t)ol_n) != 0) return -ENOMEM;
                if (append_byte(buffer, cap, &used, '"') != 0) return -ENOMEM;
                break;
            }
            default:
                return -EINVAL;
            }
        }
        if (append_byte(buffer, cap, &used, '}') != 0) return -ENOMEM;
        *length = used;
        return 0;
    }

    if (content_format == SIRENA_LWM2M_CF_SENML_JSON) {
        {
            bool same_base = true;
            size_t si;
            for (si = 1U; si < value_count; ++si) {
                if (values[si].object_id != values[0].object_id ||
                    values[si].instance_id != values[0].instance_id) {
                    same_base = false;
                    break;
                }
            }
            if (same_base) {
                char bn[64];
                snprintf(bn, sizeof(bn), "{\"bn\":\"/%u/%u/\",\"e\":[", values[0].object_id, values[0].instance_id);
                if (append(buffer, cap, &used, bn, strlen(bn)) != 0) return -ENOMEM;
            } else {
                if (append(buffer, cap, &used, "{\"e\":[", 6U) != 0) return -ENOMEM;
            }
        }
        for (i = 0U; i < value_count; ++i) {
            char tmp[128];
            int n;
            if (i != 0U) {
                if (append_byte(buffer, cap, &used, ',') != 0) return -ENOMEM;
            }
            {
                bool same_base = true;
                size_t si;
                for (si = 1U; si < value_count; ++si) {
                    if (values[si].object_id != values[0].object_id ||
                        values[si].instance_id != values[0].instance_id) {
                        same_base = false;
                        break;
                    }
                }
                if (same_base) {
                    n = snprintf(tmp, sizeof(tmp), "{\"n\":\"%u\",\"v\":", effective_resource_id(&values[i]));
                } else {
                    n = snprintf(tmp, sizeof(tmp), "{\"n\":\"/%u/%u/%u\",\"v\":", values[i].object_id,
                                 values[i].instance_id, effective_resource_id(&values[i]));
                }
            }
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
            case SIRENA_LWM2M_VALUE_OBJECT_LINK: {
                char ol[32];
                int ol_n = snprintf(ol, sizeof(ol), "%u:%u",
                                    values[i].data.object_link.object_id,
                                    values[i].data.object_link.instance_id);
                if (ol_n < 0) return -ENOMEM;
                if (append_byte(buffer, cap, &used, '"') != 0) return -ENOMEM;
                if (append(buffer, cap, &used, ol, (size_t)ol_n) != 0) return -ENOMEM;
                if (append_byte(buffer, cap, &used, '"') != 0) return -ENOMEM;
                break;
            }
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
            case SIRENA_LWM2M_VALUE_OBJECT_LINK: {
                char ol[32];
                int ol_n = snprintf(ol, sizeof(ol), "%u:%u",
                                    values[i].data.object_link.object_id,
                                    values[i].data.object_link.instance_id);
                if (ol_n < 0) return -ENOMEM;
                if (append_byte(buffer, cap, &used, '"') != 0) return -ENOMEM;
                if (append(buffer, cap, &used, ol, (size_t)ol_n) != 0) return -ENOMEM;
                if (append_byte(buffer, cap, &used, '"') != 0) return -ENOMEM;
                break;
            }
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
    case SIRENA_LWM2M_VALUE_OBJECT_LINK:
        if (value_len != 4U) {
            return -EINVAL;
        }
        v->type = SIRENA_LWM2M_VALUE_OBJECT_LINK;
        v->data.object_link.object_id = ((uint16_t)buf[value_start] << 8) | buf[value_start + 1];
        v->data.object_link.instance_id = ((uint16_t)buf[value_start + 2] << 8) | buf[value_start + 3];
        break;
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

static int decode_cbor_value(const uint8_t *buf, size_t len,
                            enum sirena_lwm2m_value_type expected,
                            struct sirena_lwm2m_codec_value *v,
                            size_t *consumed)
{
    size_t pos = 0U;
    uint8_t head;
    uint8_t major;
    uint64_t arg = 0U;
    bool has_arg = false;

    if (len == 0U) {
        return -EINVAL;
    }
    head = buf[0];
    major = head >> 5;
    if ((head & 0x1FU) <= 23U) {
        arg = head & 0x1FU;
        pos = 1U;
        has_arg = true;
    } else if ((head & 0x1FU) == 24U) {
        if (pos + 2U > len) return -EINVAL;
        arg = buf[1];
        pos = 2U;
        has_arg = true;
    } else if ((head & 0x1FU) == 25U) {
        if (pos + 3U > len) return -EINVAL;
        arg = ((uint64_t)buf[1] << 8) | buf[2];
        pos = 3U;
        has_arg = true;
    } else if ((head & 0x1FU) == 26U) {
        if (pos + 5U > len) return -EINVAL;
        arg = ((uint64_t)buf[1] << 24) | ((uint64_t)buf[2] << 16) |
             ((uint64_t)buf[3] << 8) | buf[4];
        pos = 5U;
        has_arg = true;
    } else if ((head & 0x1FU) == 27U) {
        if (pos + 9U > len) return -EINVAL;
        arg = 0U;
        for (size_t i = 0U; i < 8U; ++i) {
            arg = (arg << 8) | buf[1U + i];
        }
        pos = 9U;
        has_arg = true;
    } else {
        return -EINVAL;
    }

    v->object_id = 0U;
    v->instance_id = 0U;
    v->resource_id = 0U;
    v->id = 0U;

    switch (expected) {
    case SIRENA_LWM2M_VALUE_BOOLEAN:
        if (head == 0xF4) {
            v->type = SIRENA_LWM2M_VALUE_BOOLEAN;
            v->data.boolean = false;
        } else if (head == 0xF5) {
            v->type = SIRENA_LWM2M_VALUE_BOOLEAN;
            v->data.boolean = true;
        } else {
            return -EINVAL;
        }
        pos = 1U;
        break;
    case SIRENA_LWM2M_VALUE_INTEGER:
        if (major == 0U) {
            v->type = SIRENA_LWM2M_VALUE_INTEGER;
            v->data.integer = (int64_t)arg;
        } else if (major == 1U) {
            v->type = SIRENA_LWM2M_VALUE_INTEGER;
            if (arg == UINT64_MAX) return -EINVAL;
            v->data.integer = -(int64_t)(arg + 1U);
        } else {
            return -EINVAL;
        }
        break;
    case SIRENA_LWM2M_VALUE_UNSIGNED:
        if (major != 0U) return -EINVAL;
        v->type = SIRENA_LWM2M_VALUE_UNSIGNED;
        v->data.unsigned_integer = arg;
        break;
    case SIRENA_LWM2M_VALUE_FLOAT:
        pos = 1U;
        if (head == 0xFB) {
            uint64_t bits = 0U;
            if (pos + 8U > len) return -EINVAL;
            for (size_t i = 0U; i < 8U; ++i) {
                bits = (bits << 8) | buf[pos + i];
            }
            memcpy(&v->data.floating_point, &bits, sizeof(v->data.floating_point));
            v->type = SIRENA_LWM2M_VALUE_FLOAT;
            pos += 8U;
        } else if (head == 0xFA) {
            uint32_t bits = 0U;
            float single;
            if (pos + 4U > len) return -EINVAL;
            for (size_t i = 0U; i < 4U; ++i) {
                bits = (bits << 8) | buf[pos + i];
            }
            memcpy(&single, &bits, sizeof(single));
            v->data.floating_point = (double)single;
            v->type = SIRENA_LWM2M_VALUE_FLOAT;
            pos += 4U;
        } else {
            return -EINVAL;
        }
        break;
    case SIRENA_LWM2M_VALUE_STRING:
        if (major != 3U) return -EINVAL;
        if (pos + arg > len) return -EINVAL;
        v->type = SIRENA_LWM2M_VALUE_STRING;
        v->data.string.value = (const char *)&buf[pos];
        v->data.string.length = (size_t)arg;
        pos += (size_t)arg;
        break;
    case SIRENA_LWM2M_VALUE_OPAQUE:
        if (major != 2U) return -EINVAL;
        if (pos + arg > len) return -EINVAL;
        v->type = SIRENA_LWM2M_VALUE_OPAQUE;
        v->data.opaque.value = &buf[pos];
        v->data.opaque.length = (size_t)arg;
        pos += (size_t)arg;
        break;
    case SIRENA_LWM2M_VALUE_OBJECT_LINK:
        if (major != 2U || arg != 4U) return -EINVAL;
        if (pos + 4U > len) return -EINVAL;
        v->type = SIRENA_LWM2M_VALUE_OBJECT_LINK;
        v->data.object_link.object_id = ((uint16_t)buf[pos] << 8) | buf[pos + 1];
        v->data.object_link.instance_id = ((uint16_t)buf[pos + 2] << 8) | buf[pos + 3];
        pos += 4U;
        break;
    default:
        return -EINVAL;
    }

    *consumed = pos;
    (void)has_arg;
    return 0;
}

static int decode_json_value(const char *buf, size_t len,
                            enum sirena_lwm2m_value_type expected,
                            struct sirena_lwm2m_codec_value *v)
{
    size_t pos = 0U;
    size_t key_start;
    size_t key_len;
    uint16_t id = 0U;

    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != '{') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != '"') return -EINVAL;
    pos++;
    key_start = pos;
    while (pos < len && buf[pos] != '"') {
        if (buf[pos] == '\\') {
            pos++;
            if (pos >= len) return -EINVAL;
        }
        pos++;
    }
    if (pos >= len) return -EINVAL;
    key_len = pos - key_start;
    if (key_len == 0U) return -EINVAL;
    for (size_t i = 0U; i < key_len; ++i) {
        if (buf[key_start + i] < '0' || buf[key_start + i] > '9') return -EINVAL;
        id = (uint16_t)(id * 10U + (uint16_t)(buf[key_start + i] - '0'));
    }
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != ':') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;

    v->object_id = 0U;
    v->instance_id = 0U;
    v->resource_id = id;
    v->id = id;
    v->type = expected;

    switch (expected) {
    case SIRENA_LWM2M_VALUE_BOOLEAN:
        if (pos + 4U <= len && strncmp(buf + pos, "true", 4U) == 0) {
            v->data.boolean = true;
            pos += 4U;
        } else if (pos + 5U <= len && strncmp(buf + pos, "false", 5U) == 0) {
            v->data.boolean = false;
            pos += 5U;
        } else {
            return -EINVAL;
        }
        break;
    case SIRENA_LWM2M_VALUE_INTEGER: {
        size_t start = pos;
        bool neg = false;
        int64_t val = 0;
        if (pos < len && buf[pos] == '-') { neg = true; pos++; }
        if (pos >= len || buf[pos] < '0' || buf[pos] > '9') return -EINVAL;
        while (pos < len && buf[pos] >= '0' && buf[pos] <= '9') {
            val = val * 10 + (int64_t)(buf[pos] - '0');
            pos++;
        }
        v->data.integer = neg ? -val : val;
        (void)start;
        break;
    }
    case SIRENA_LWM2M_VALUE_UNSIGNED: {
        uint64_t val = 0U;
        if (pos >= len || buf[pos] < '0' || buf[pos] > '9') return -EINVAL;
        while (pos < len && buf[pos] >= '0' && buf[pos] <= '9') {
            val = val * 10U + (uint64_t)(buf[pos] - '0');
            pos++;
        }
        v->data.unsigned_integer = val;
        break;
    }
    case SIRENA_LWM2M_VALUE_FLOAT: {
        char tmp[65];
        char *endptr = NULL;
        size_t avail = len - pos;
        double val;
        if (avail == 0U || avail > (sizeof(tmp) - 1U)) return -EINVAL;
        memcpy(tmp, buf + pos, avail);
        tmp[avail] = '\0';
        val = strtod(tmp, &endptr);
        if (endptr == tmp) return -EINVAL;
        pos += (size_t)(endptr - tmp);
        v->data.floating_point = val;
        break;
    }
    case SIRENA_LWM2M_VALUE_STRING: {
        size_t start;
        if (pos >= len || buf[pos] != '"') return -EINVAL;
        pos++;
        start = pos;
        while (pos < len && buf[pos] != '"') {
            if (buf[pos] == '\\') {
                pos++;
                if (pos >= len) return -EINVAL;
            }
            pos++;
        }
        if (pos >= len) return -EINVAL;
        v->data.string.value = &buf[start];
        v->data.string.length = pos - start;
        pos++;
        break;
    }
    case SIRENA_LWM2M_VALUE_OPAQUE: {
        size_t start;
        if (pos >= len || buf[pos] != '"') return -EINVAL;
        pos++;
        start = pos;
        while (pos < len && buf[pos] != '"') {
            if (buf[pos] == '\\') {
                pos++;
                if (pos >= len) return -EINVAL;
            }
            pos++;
        }
        if (pos >= len) return -EINVAL;
        v->data.opaque.value = &buf[start];
        v->data.opaque.length = pos - start;
        pos++;
        break;
    }
    case SIRENA_LWM2M_VALUE_OBJECT_LINK: {
        size_t start;
        size_t content_len;
        const char *colon;
        uint16_t oid = 0U;
        uint16_t iid = 0U;
        const char *p;
        if (pos >= len || buf[pos] != '"') return -EINVAL;
        pos++;
        start = pos;
        while (pos < len && buf[pos] != '"') {
            if (buf[pos] == '\\') {
                pos++;
                if (pos >= len) return -EINVAL;
            }
            pos++;
        }
        if (pos >= len) return -EINVAL;
        content_len = pos - start;
        colon = memchr(&buf[start], ':', content_len);
        if (colon == NULL) return -EINVAL;
        p = &buf[start];
        while (p < colon) {
            if (*p < '0' || *p > '9') return -EINVAL;
            oid = (uint16_t)(oid * 10U + (uint16_t)(*p - '0'));
            p++;
        }
        if (p == &buf[start]) return -EINVAL;
        p = colon + 1;
        while (p < &buf[start] + content_len) {
            if (*p < '0' || *p > '9') return -EINVAL;
            iid = (uint16_t)(iid * 10U + (uint16_t)(*p - '0'));
            p++;
        }
        if (p == colon + 1) return -EINVAL;
        v->data.object_link.object_id = oid;
        v->data.object_link.instance_id = iid;
        pos++;
        break;
    }
    default:
        return -EINVAL;
    }

    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != '}') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos != len) return -EINVAL;
    return 0;
}

static int decode_senml_json(const char *buf, size_t len,
                              enum sirena_lwm2m_value_type expected,
                              struct sirena_lwm2m_codec_value *v)
{
    size_t pos = 0U;
    char base[64];
    size_t bn_start;
    size_t bn_len;
    int object_id = 0;
    int instance_id = 0;
    int resource_id = 0;
    size_t rid_start;
    size_t rid_len;
    size_t tpos;
    size_t tlen;
    size_t j;
    int in_str = 0;

    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != '{') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos + 4U > len || strncmp(buf + pos, "\"bn\"", 4U) != 0) return -EINVAL;
    pos += 4U;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != ':') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != '"') return -EINVAL;
    pos++;
    bn_start = pos;
    while (pos < len && buf[pos] != '"') {
        if (buf[pos] == '\\') {
            pos++;
            if (pos >= len) return -EINVAL;
        }
        pos++;
    }
    if (pos >= len) return -EINVAL;
    bn_len = pos - bn_start;
    if (bn_len == 0U || bn_len >= sizeof(base)) return -EINVAL;
    memcpy(base, buf + bn_start, bn_len);
    base[bn_len] = '\0';
    pos++;
    if (sscanf(base, "/%d/%d/", &object_id, &instance_id) != 2) return -EINVAL;
    if (object_id < 0 || object_id > 65535 || instance_id < 0 || instance_id > 65535) return -EINVAL;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != ',') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos + 3U > len || strncmp(buf + pos, "\"e\"", 3U) != 0) return -EINVAL;
    pos += 3U;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != ':') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != '[') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != '{') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos + 3U > len || strncmp(buf + pos, "\"n\"", 3U) != 0) return -EINVAL;
    pos += 3U;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != ':') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != '"') return -EINVAL;
    pos++;
    rid_start = pos;
    while (pos < len && buf[pos] != '"') pos++;
    if (pos >= len) return -EINVAL;
    rid_len = pos - rid_start;
    if (rid_len == 0U) return -EINVAL;
    for (size_t i = 0U; i < rid_len; ++i) {
        if (buf[rid_start + i] < '0' || buf[rid_start + i] > '9') return -EINVAL;
        resource_id = resource_id * 10 + (buf[rid_start + i] - '0');
    }
    if (resource_id < 0 || resource_id > 65535) return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != ',') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos + 3U > len || strncmp(buf + pos, "\"v\"", 3U) != 0) return -EINVAL;
    pos += 3U;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != ':') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    tpos = pos;
    j = pos;
    in_str = 0;
    while (j < len) {
        if (buf[j] == '"') { in_str = !in_str; j++; continue; }
        if (!in_str && buf[j] == '}') break;
        j++;
    }
    if (j >= len) return -EINVAL;
    tlen = j - pos;
    while (tlen > 0U && (buf[tpos + tlen - 1U] == ' ' || buf[tpos + tlen - 1U] == '\t' || buf[tpos + tlen - 1U] == '\n' || buf[tpos + tlen - 1U] == '\r')) tlen--;
    if (tlen == 0U) return -EINVAL;

    v->object_id = (uint16_t)object_id;
    v->instance_id = (uint16_t)instance_id;
    v->resource_id = (uint16_t)resource_id;
    v->id = (uint16_t)resource_id;
    v->type = expected;

    switch (expected) {
    case SIRENA_LWM2M_VALUE_BOOLEAN:
        if (tlen == 4U && strncmp(buf + tpos, "true", 4U) == 0) v->data.boolean = true;
        else if (tlen == 5U && strncmp(buf + tpos, "false", 5U) == 0) v->data.boolean = false;
        else return -EINVAL;
        break;
    case SIRENA_LWM2M_VALUE_INTEGER: {
        size_t k = 0U;
        int neg = 0;
        int64_t val = 0;
        if (tpos < len && buf[tpos] == '-') { neg = 1; k++; }
        if (k >= tlen || buf[tpos + k] < '0' || buf[tpos + k] > '9') return -EINVAL;
        while (k < tlen && buf[tpos + k] >= '0' && buf[tpos + k] <= '9') {
            val = val * 10 + (int64_t)(buf[tpos + k] - '0');
            k++;
        }
        if (k != tlen) return -EINVAL;
        v->data.integer = neg ? -val : val;
        break;
    }
    case SIRENA_LWM2M_VALUE_UNSIGNED: {
        uint64_t val = 0U;
        size_t k = 0U;
        if (tlen == 0U) return -EINVAL;
        while (k < tlen) {
            if (buf[tpos + k] < '0' || buf[tpos + k] > '9') return -EINVAL;
            val = val * 10U + (uint64_t)(buf[tpos + k] - '0');
            k++;
        }
        v->data.unsigned_integer = val;
        break;
    }
    case SIRENA_LWM2M_VALUE_FLOAT: {
        char tmp[65];
        char *endptr = NULL;
        double val;
        if (tlen == 0U || tlen + 1U > sizeof(tmp)) return -EINVAL;
        memcpy(tmp, buf + tpos, tlen);
        tmp[tlen] = '\0';
        val = strtod(tmp, &endptr);
        if (endptr == tmp || (size_t)(endptr - tmp) != tlen) return -EINVAL;
        v->data.floating_point = val;
        break;
    }
    case SIRENA_LWM2M_VALUE_STRING:
        if (tlen < 2U || buf[tpos] != '"' || buf[tpos + tlen - 1U] != '"') return -EINVAL;
        v->data.string.value = &buf[tpos + 1U];
        v->data.string.length = tlen - 2U;
        break;
    case SIRENA_LWM2M_VALUE_OPAQUE:
        if (tlen < 2U || buf[tpos] != '"' || buf[tpos + tlen - 1U] != '"') return -EINVAL;
        v->data.opaque.value = &buf[tpos + 1U];
        v->data.opaque.length = tlen - 2U;
        break;
    case SIRENA_LWM2M_VALUE_OBJECT_LINK: {
        size_t inner_start = tpos + 1U;
        size_t inner_len = tlen - 2U;
        const char *colon;
        uint16_t oid = 0U;
        uint16_t iid = 0U;
        const char *p;
        if (tlen < 2U || buf[tpos] != '"' || buf[tpos + tlen - 1U] != '"') return -EINVAL;
        colon = memchr(buf + inner_start, ':', inner_len);
        if (colon == NULL) return -EINVAL;
        p = buf + inner_start;
        while (p < colon) {
            if (*p < '0' || *p > '9') return -EINVAL;
            oid = (uint16_t)(oid * 10U + (uint16_t)(*p - '0'));
            p++;
        }
        if (p == buf + inner_start) return -EINVAL;
        p = colon + 1;
        while (p < buf + inner_start + inner_len) {
            if (*p < '0' || *p > '9') return -EINVAL;
            iid = (uint16_t)(iid * 10U + (uint16_t)(*p - '0'));
            p++;
        }
        if (p == colon + 1) return -EINVAL;
        v->data.object_link.object_id = oid;
        v->data.object_link.instance_id = iid;
        break;
    }
    default:
        return -EINVAL;
    }

    pos = tpos + tlen;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != '}') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != ']') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != '}') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos != len) return -EINVAL;
    return 0;
}

static int decode_senml_etch_json(const char *buf, size_t len,
                                 enum sirena_lwm2m_value_type expected,
                                 struct sirena_lwm2m_codec_value *v)
{
    size_t pos = 0U;
    int object_id = 0;
    int instance_id = 0;
    int resource_id = 0;
    char path[64];
    size_t path_len;
    size_t tstart;
    size_t tlen;
    int in_str = 0;

    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != '{') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos + 4U > len || strncmp(buf + pos, "\"e\"", 4U) != 0) return -EINVAL;
    pos += 4U;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != ':') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != '[') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != '{') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos + 3U > len || strncmp(buf + pos, "\"n\"", 3U) != 0) return -EINVAL;
    pos += 3U;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != ':') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != '"') return -EINVAL;
    pos++;
    path_len = 0U;
    while (pos < len && buf[pos] != '"') {
        if (buf[pos] == '\\') {
            pos++;
            if (pos >= len) return -EINVAL;
        }
        if (path_len + 1U >= sizeof(path)) return -EINVAL;
        path[path_len++] = buf[pos++];
    }
    if (pos >= len) return -EINVAL;
    path[path_len] = '\0';
    pos++;
    if (sscanf(path, "/%d/%d/%d", &object_id, &instance_id, &resource_id) != 3) return -EINVAL;
    if (object_id < 0 || object_id > 65535 || instance_id < 0 || instance_id > 65535 ||
        resource_id < 0 || resource_id > 65535) return -EINVAL;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != ',') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos + 3U > len || strncmp(buf + pos, "\"v\"", 3U) != 0) return -EINVAL;
    pos += 3U;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    if (pos >= len || buf[pos] != ':') return -EINVAL;
    pos++;
    while (pos < len && (buf[pos] == ' ' || buf[pos] == '\t' || buf[pos] == '\n' || buf[pos] == '\r')) pos++;
    tstart = pos;
    in_str = 0;
    while (pos < len) {
        if (buf[pos] == '"') { in_str = !in_str; pos++; continue; }
        if (!in_str && (buf[pos] == ',' || buf[pos] == '}')) break;
        pos++;
    }
    if (pos >= len) return -EINVAL;
    tlen = pos - tstart;
    while (tlen > 0U && (buf[tstart + tlen - 1U] == ' ' || buf[tstart + tlen - 1U] == '\t' ||
                          buf[tstart + tlen - 1U] == '\n' || buf[tstart + tlen - 1U] == '\r')) tlen--;
    if (tlen == 0U) return -EINVAL;

    v->object_id = (uint16_t)object_id;
    v->instance_id = (uint16_t)instance_id;
    v->resource_id = (uint16_t)resource_id;
    v->id = (uint16_t)resource_id;
    v->type = expected;

    switch (expected) {
    case SIRENA_LWM2M_VALUE_BOOLEAN:
        if (tlen == 4U && strncmp(buf + tstart, "true", 4U) == 0) v->data.boolean = true;
        else if (tlen == 5U && strncmp(buf + tstart, "false", 5U) == 0) v->data.boolean = false;
        else return -EINVAL;
        break;
    case SIRENA_LWM2M_VALUE_INTEGER: {
        size_t k = 0U;
        int neg = 0;
        int64_t val = 0;
        if (buf[tstart] == '-') { neg = 1; k++; }
        if (k >= tlen || buf[tstart + k] < '0' || buf[tstart + k] > '9') return -EINVAL;
        while (k < tlen && buf[tstart + k] >= '0' && buf[tstart + k] <= '9') {
            val = val * 10 + (int64_t)(buf[tstart + k] - '0');
            k++;
        }
        if (k != tlen) return -EINVAL;
        v->data.integer = neg ? -val : val;
        break;
    }
    case SIRENA_LWM2M_VALUE_UNSIGNED: {
        uint64_t val = 0U;
        size_t k = 0U;
        while (k < tlen) {
            if (buf[tstart + k] < '0' || buf[tstart + k] > '9') return -EINVAL;
            val = val * 10U + (uint64_t)(buf[tstart + k] - '0');
            k++;
        }
        if (k != tlen) return -EINVAL;
        v->data.unsigned_integer = val;
        break;
    }
    case SIRENA_LWM2M_VALUE_FLOAT: {
        char tmp[65];
        char *endptr = NULL;
        double val;
        if (tlen == 0U || tlen + 1U > sizeof(tmp)) return -EINVAL;
        memcpy(tmp, buf + tstart, tlen);
        tmp[tlen] = '\0';
        val = strtod(tmp, &endptr);
        if (endptr == tmp || (size_t)(endptr - tmp) != tlen) return -EINVAL;
        v->data.floating_point = val;
        break;
    }
    case SIRENA_LWM2M_VALUE_STRING:
        if (tlen < 2U || buf[tstart] != '"' || buf[tstart + tlen - 1U] != '"') return -EINVAL;
        v->data.string.value = &buf[tstart + 1U];
        v->data.string.length = tlen - 2U;
        break;
    case SIRENA_LWM2M_VALUE_OPAQUE:
        if (tlen < 2U || buf[tstart] != '"' || buf[tstart + tlen - 1U] != '"') return -EINVAL;
        v->data.opaque.value = &buf[tstart + 1U];
        v->data.opaque.length = tlen - 2U;
        break;
    case SIRENA_LWM2M_VALUE_OBJECT_LINK: {
        size_t inner_start = tstart + 1U;
        size_t inner_len = tlen - 2U;
        const char *colon;
        uint16_t oid = 0U;
        uint16_t iid = 0U;
        const char *p;
        if (tlen < 2U || buf[tstart] != '"' || buf[tstart + tlen - 1U] != '"') return -EINVAL;
        colon = memchr(buf + inner_start, ':', inner_len);
        if (colon == NULL) return -EINVAL;
        p = buf + inner_start;
        while (p < colon) {
            if (*p < '0' || *p > '9') return -EINVAL;
            oid = (uint16_t)(oid * 10U + (uint16_t)(*p - '0'));
            p++;
        }
        if (p == buf + inner_start) return -EINVAL;
        p = colon + 1;
        while (p < buf + inner_start + inner_len) {
            if (*p < '0' || *p > '9') return -EINVAL;
            iid = (uint16_t)(iid * 10U + (uint16_t)(*p - '0'));
            p++;
        }
        if (p == colon + 1) return -EINVAL;
        v->data.object_link.object_id = oid;
        v->data.object_link.instance_id = iid;
        break;
    }
    default:
        return -EINVAL;
    }

    return 0;
}

static int read_cbor_head(const uint8_t *buf, size_t len, size_t *pos,
                         uint8_t *major, uint64_t *arg)
{
    uint8_t head;
    if (*pos >= len) return -EINVAL;
    head = buf[*pos];
    *major = head >> 5;
    if ((head & 0x1FU) <= 23U) {
        *arg = head & 0x1FU;
        (*pos)++;
        return 0;
    }
    if ((head & 0x1FU) == 24U) {
        if (*pos + 2U > len) return -EINVAL;
        *arg = buf[*pos + 1U];
        *pos += 2U;
        return 0;
    }
    if ((head & 0x1FU) == 25U) {
        if (*pos + 3U > len) return -EINVAL;
        *arg = ((uint64_t)buf[*pos + 1U] << 8) | buf[*pos + 2U];
        *pos += 3U;
        return 0;
    }
    if ((head & 0x1FU) == 26U) {
        if (*pos + 5U > len) return -EINVAL;
        *arg = ((uint64_t)buf[*pos + 1U] << 24) |
               ((uint64_t)buf[*pos + 2U] << 16) |
               ((uint64_t)buf[*pos + 3U] << 8) | buf[*pos + 4U];
        *pos += 5U;
        return 0;
    }
    if ((head & 0x1FU) == 27U) {
        if (*pos + 9U > len) return -EINVAL;
        *arg = 0U;
        for (size_t i = 0U; i < 8U; ++i) {
            *arg = (*arg << 8) | buf[*pos + 1U + i];
        }
        *pos += 9U;
        return 0;
    }
    return -EINVAL;
}

static int decode_senml_cbor(const uint8_t *buf, size_t len,
                            enum sirena_lwm2m_value_type expected,
                            struct sirena_lwm2m_codec_value *v)
{
    size_t pos = 0U;
    uint8_t major;
    uint64_t arg;
    char base[64];
    int object_id = 0;
    int instance_id = 0;
    int resource_id = 0;
    size_t consumed = 0U;
    struct sirena_lwm2m_codec_value tmp;

    if (read_cbor_head(buf, len, &pos, &major, &arg) != 0) return -EINVAL;
    if (major != 5U || arg != 2U) return -EINVAL;

    if (read_cbor_head(buf, len, &pos, &major, &arg) != 0) return -EINVAL;
    if (major != 3U || arg != 2U) return -EINVAL;
    if (pos + 2U > len || memcmp(buf + pos, "bn", 2U) != 0) return -EINVAL;
    pos += 2U;

    if (read_cbor_head(buf, len, &pos, &major, &arg) != 0) return -EINVAL;
    if (major != 3U || arg == 0U || arg >= sizeof(base)) return -EINVAL;
    if (pos + (size_t)arg > len) return -EINVAL;
    memcpy(base, buf + pos, (size_t)arg);
    base[arg] = '\0';
    pos += (size_t)arg;
    if (sscanf(base, "/%d/%d/", &object_id, &instance_id) != 2) return -EINVAL;
    if (object_id < 0 || object_id > 65535 || instance_id < 0 || instance_id > 65535) return -EINVAL;

    if (read_cbor_head(buf, len, &pos, &major, &arg) != 0) return -EINVAL;
    if (major != 3U || arg != 1U) return -EINVAL;
    if (pos + 1U > len || buf[pos] != 'e') return -EINVAL;
    pos += 1U;

    if (read_cbor_head(buf, len, &pos, &major, &arg) != 0) return -EINVAL;
    if (major != 4U || arg != 1U) return -EINVAL;

    if (read_cbor_head(buf, len, &pos, &major, &arg) != 0) return -EINVAL;
    if (major != 5U || arg != 2U) return -EINVAL;

    if (read_cbor_head(buf, len, &pos, &major, &arg) != 0) return -EINVAL;
    if (major != 3U || arg != 1U) return -EINVAL;
    if (pos + 1U > len || buf[pos] != 'n') return -EINVAL;
    pos += 1U;

    if (read_cbor_head(buf, len, &pos, &major, &arg) != 0) return -EINVAL;
    if (major != 3U || arg == 0U || arg > 16U) return -EINVAL;
    if (pos + (size_t)arg > len) return -EINVAL;
    for (size_t i = 0U; i < (size_t)arg; ++i) {
        if (buf[pos + i] < '0' || buf[pos + i] > '9') return -EINVAL;
        resource_id = resource_id * 10 + (buf[pos + i] - '0');
    }
    if (resource_id < 0 || resource_id > 65535) return -EINVAL;
    pos += (size_t)arg;

    if (read_cbor_head(buf, len, &pos, &major, &arg) != 0) return -EINVAL;
    if (major != 3U || arg != 1U) return -EINVAL;
    if (pos + 1U > len || buf[pos] != 'v') return -EINVAL;
    pos += 1U;

    memset(&tmp, 0, sizeof(tmp));
    if (decode_cbor_value(buf + pos, len - pos, expected, &tmp, &consumed) != 0) return -EINVAL;
    if (consumed == 0U || pos + consumed > len) return -EINVAL;
    pos += consumed;

    if (pos != len) return -EINVAL;

    tmp.object_id = (uint16_t)object_id;
    tmp.instance_id = (uint16_t)instance_id;
    tmp.resource_id = (uint16_t)resource_id;
    tmp.id = (uint16_t)resource_id;
    *v = tmp;
    return 0;
}

static int decode_senml_etch_cbor(const uint8_t *buf, size_t len,
                                 enum sirena_lwm2m_value_type expected,
                                 struct sirena_lwm2m_codec_value *v)
{
    size_t pos = 0U;
    uint8_t major;
    uint64_t arg;
    char path[64];
    int object_id = 0;
    int instance_id = 0;
    int resource_id = 0;
    size_t consumed = 0U;
    struct sirena_lwm2m_codec_value tmp;

    if (read_cbor_head(buf, len, &pos, &major, &arg) != 0) return -EINVAL;
    if (major != 4U || arg != 1U) return -EINVAL;

    if (read_cbor_head(buf, len, &pos, &major, &arg) != 0) return -EINVAL;
    if (major != 5U || arg != 3U) return -EINVAL;

    if (read_cbor_head(buf, len, &pos, &major, &arg) != 0) return -EINVAL;
    if (major != 3U || arg != 1U) return -EINVAL;
    if (pos + 1U > len || buf[pos] != 'v') return -EINVAL;
    pos += 1U;

    memset(&tmp, 0, sizeof(tmp));
    if (decode_cbor_value(buf + pos, len - pos, expected, &tmp, &consumed) != 0) return -EINVAL;
    if (consumed == 0U || pos + consumed > len) return -EINVAL;
    pos += consumed;

    if (read_cbor_head(buf, len, &pos, &major, &arg) != 0) return -EINVAL;
    if (major != 3U || arg != 1U) return -EINVAL;
    if (pos + 1U > len || buf[pos] != 'n') return -EINVAL;
    pos += 1U;

    if (read_cbor_head(buf, len, &pos, &major, &arg) != 0) return -EINVAL;
    if (major != 3U || arg == 0U || arg >= sizeof(path)) return -EINVAL;
    if (pos + (size_t)arg > len) return -EINVAL;
    memcpy(path, buf + pos, (size_t)arg);
    path[arg] = '\0';
    pos += (size_t)arg;
    if (sscanf(path, "/%d/%d/%d", &object_id, &instance_id, &resource_id) != 3) return -EINVAL;
    if (object_id < 0 || object_id > 65535 || instance_id < 0 || instance_id > 65535 ||
        resource_id < 0 || resource_id > 65535) return -EINVAL;

    if (read_cbor_head(buf, len, &pos, &major, &arg) != 0) return -EINVAL;
    if (major != 3U || arg != 2U) return -EINVAL;
    if (pos + 2U > len || memcmp(buf + pos, "vb", 2U) != 0) return -EINVAL;
    pos += 2U;
    if (pos >= len || buf[pos] != 0xF5) return -EINVAL;
    pos += 1U;

    if (pos != len) return -EINVAL;

    tmp.object_id = (uint16_t)object_id;
    tmp.instance_id = (uint16_t)instance_id;
    tmp.resource_id = (uint16_t)resource_id;
    tmp.id = (uint16_t)resource_id;
    *v = tmp;
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

    if (content_format == SIRENA_LWM2M_CF_JSON) {
        if (cap < 1U) return -ENOMEM;
        if (decode_json_value((const char *)buffer, length, type, &values[0]) != 0) {
            return -EINVAL;
        }
        *value_count = 1U;
        return 0;
    }

    if (content_format == SIRENA_LWM2M_CF_CBOR) {
        size_t consumed = 0U;
        if (cap < 1U) return -ENOMEM;
        if (decode_cbor_value((const uint8_t *)buffer, length, type, &values[0],
                              &consumed) != 0) {
            return -EINVAL;
        }
        if (consumed != length) return -EINVAL;
        *value_count = 1U;
        return 0;
    }

    if (content_format == SIRENA_LWM2M_CF_SENML_CBOR) {
        if (cap < 1U) return -ENOMEM;
        if (decode_senml_cbor((const uint8_t *)buffer, length, type, &values[0]) != 0) {
            return -EINVAL;
        }
        *value_count = 1U;
        return 0;
    }

    if (content_format == SIRENA_LWM2M_CF_SENML_JSON) {
        if (cap < 1U) return -ENOMEM;
        if (decode_senml_json((const char *)buffer, length, type, &values[0]) != 0) {
            return -EINVAL;
        }
        *value_count = 1U;
        return 0;
    }

    if (content_format == SIRENA_LWM2M_CF_SENML_ETCH_JSON) {
        if (cap < 1U) return -ENOMEM;
        if (decode_senml_etch_json((const char *)buffer, length, type, &values[0]) != 0) {
            return -EINVAL;
        }
        *value_count = 1U;
        return 0;
    }

    if (content_format == SIRENA_LWM2M_CF_SENML_ETCH_CBOR) {
        if (cap < 1U) return -ENOMEM;
        if (decode_senml_etch_cbor((const uint8_t *)buffer, length, type, &values[0]) != 0) {
            return -EINVAL;
        }
        *value_count = 1U;
        return 0;
    }

    (void)buffer;
    (void)length;
    (void)values;

    return -ENOTSUP;
}

static int senml_json_skip_ws(const char *buf, size_t len, size_t *pos)
{
    while (*pos < len) {
        char c = buf[*pos];

        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            break;
        }
        (*pos)++;
    }

    return 0;
}

static int senml_json_read_string(const char *buf, size_t len, size_t *pos, char *out,
                                  size_t out_size)
{
    size_t start;
    size_t length;

    if (*pos >= len || buf[*pos] != '"') {
        return -EINVAL;
    }

    (*pos)++;
    start = *pos;

    while (*pos < len && buf[*pos] != '"') {
        if (buf[*pos] == '\\') {
            return -EINVAL;
        }
        (*pos)++;
    }

    if (*pos >= len) {
        return -EINVAL;
    }

    length = *pos - start;
    (*pos)++;

    if (length >= out_size) {
        return -ENOSPC;
    }

    memcpy(out, buf + start, length);
    out[length] = '\0';

    return (int)length;
}

static int senml_json_skip_value(const char *buf, size_t len, size_t *pos)
{
    bool in_string = false;
    size_t depth = 0U;

    if (*pos >= len) {
        return -EINVAL;
    }

    if (buf[*pos] == '"') {
        (*pos)++;

        while (*pos < len && buf[*pos] != '"') {
            if (buf[*pos] == '\\') {
                (*pos)++;

                if (*pos >= len) {
                    return -EINVAL;
                }
            }
            (*pos)++;
        }

        if (*pos >= len) {
            return -EINVAL;
        }

        (*pos)++;

        return 0;
    }

    if (buf[*pos] == '{' || buf[*pos] == '[') {
        while (*pos < len) {
            char c = buf[*pos];

            if (in_string) {
                if (c == '\\') {
                    (*pos)++;

                    if (*pos >= len) {
                        return -EINVAL;
                    }
                } else if (c == '"') {
                    in_string = false;
                }
            } else if (c == '"') {
                in_string = true;
            } else if (c == '{' || c == '[') {
                depth++;
            } else if (c == '}' || c == ']') {
                depth--;

                if (depth == 0U) {
                    (*pos)++;

                    return 0;
                }
            }

            (*pos)++;
        }

        return -EINVAL;
    }

    while (*pos < len) {
        char c = buf[*pos];

        if (c == ',' || c == '}' || c == ']' || c == ' ' || c == '\t' || c == '\n' ||
            c == '\r') {
            break;
        }

        (*pos)++;
    }

    return 0;
}

static int senml_json_record(const char *buf, size_t len, size_t *pos, const char *pack_base,
                              struct sirena_lwm2m_senml_record *records, size_t *parsed,
                              size_t capacity)
{
    char base[SIRENA_LWM2M_COMPOSITE_PATH_MAX];
    char name[SIRENA_LWM2M_COMPOSITE_PATH_MAX];
    char value[SIRENA_LWM2M_COMPOSITE_VALUE_MAX];
    size_t value_length = 0U;
    bool value_is_string = false;
    bool value_invalid = false;
    bool has_name = false;
    bool has_value = false;

    strcpy(base, pack_base);

    if (*pos >= len || buf[*pos] != '{') {
        return -EINVAL;
    }

    (*pos)++;

    for (;;) {
        char key[8];
        int key_length;

        (void)senml_json_skip_ws(buf, len, pos);

        if (*pos >= len) {
            return -EINVAL;
        }

        if (buf[*pos] == '}') {
            (*pos)++;
            break;
        }

        key_length = senml_json_read_string(buf, len, pos, key, sizeof(key));

        if (key_length < 0) {
            return key_length;
        }

        (void)senml_json_skip_ws(buf, len, pos);

        if (*pos >= len || buf[*pos] != ':') {
            return -EINVAL;
        }

        (*pos)++;
        (void)senml_json_skip_ws(buf, len, pos);

        if (*pos >= len) {
            return -EINVAL;
        }

        if (strcmp(key, "bn") == 0) {
            int base_length = senml_json_read_string(buf, len, pos, base,
                                                     SIRENA_LWM2M_COMPOSITE_PATH_MAX);

            if (base_length < 0) {
                return base_length;
            }
        } else if (strcmp(key, "n") == 0) {
            int name_length = senml_json_read_string(buf, len, pos, name, sizeof(name));

            if (name_length < 0) {
                return name_length;
            }

            has_name = true;
        } else if (strcmp(key, "v") == 0) {
            if (buf[*pos] == '"') {
                int written = senml_json_read_string(buf, len, pos, value, sizeof(value));

                if (written == -ENOSPC) {
                    value_invalid = true;
                    value_length = 0U;
                    value[0] = '\0';
                } else if (written < 0) {
                    return written;
                } else {
                    value_length = (size_t)written;
                    value_is_string = true;
                }
            } else {
                size_t start = *pos;

                if (senml_json_skip_value(buf, len, pos) != 0) {
                    return -EINVAL;
                }

                value_length = *pos - start;

                while (value_length > 0U &&
                       (buf[start + value_length - 1U] == ' ' ||
                        buf[start + value_length - 1U] == '\t' ||
                        buf[start + value_length - 1U] == '\n' ||
                        buf[start + value_length - 1U] == '\r')) {
                    value_length--;
                }

                if (value_length >= sizeof(value)) {
                    value_invalid = true;
                    value_length = 0U;
                    value[0] = '\0';
                } else {
                    memcpy(value, buf + start, value_length);
                    value[value_length] = '\0';
                }
            }

            has_value = true;
        } else {
            if (senml_json_skip_value(buf, len, pos) != 0) {
                return -EINVAL;
            }
        }

        (void)senml_json_skip_ws(buf, len, pos);

        if (*pos < len && buf[*pos] == ',') {
            (*pos)++;
            continue;
        }

        if (*pos < len && buf[*pos] == '}') {
            (*pos)++;
            break;
        }

        return -EINVAL;
    }

    if (!has_name || !has_value) {
        return -EINVAL;
    }

    if (*parsed >= capacity) {
        return -ENOSPC;
    }

    if (name[0] == '/') {
        if (strlen(name) >= sizeof(records[*parsed].path)) {
            return -ENOSPC;
        }

        strcpy(records[*parsed].path, name);
    } else {
        size_t base_length = strlen(base);
        size_t name_length = strlen(name);

        if (base_length + name_length >= sizeof(records[*parsed].path)) {
            return -ENOSPC;
        }

        memcpy(records[*parsed].path, base, base_length);
        memcpy(records[*parsed].path + base_length, name, name_length + 1U);
    }

    if (value_length >= sizeof(records[*parsed].value)) {
        return -ENOSPC;
    }

    memcpy(records[*parsed].value, value, value_length);
    records[*parsed].value[value_length] = '\0';
    records[*parsed].value_length = value_length;
    records[*parsed].value_is_string = value_is_string;
    records[*parsed].value_invalid = value_invalid;
    (*parsed)++;

    return 0;
}

static int senml_json_records(const char *buf, size_t len,
                               struct sirena_lwm2m_senml_record *records, size_t *count)
{
    size_t pos = 0U;
    size_t parsed = 0U;
    char base[SIRENA_LWM2M_COMPOSITE_PATH_MAX];

    base[0] = '\0';

    (void)senml_json_skip_ws(buf, len, &pos);

    if (pos >= len) {
        return -EINVAL;
    }

    if (buf[pos] == '[') {
        pos++;

        for (;;) {
            int rc;

            (void)senml_json_skip_ws(buf, len, &pos);

            if (pos >= len) {
                return -EINVAL;
            }

            if (buf[pos] == ']') {
                pos++;
                break;
            }

            rc = senml_json_record(buf, len, &pos, base, records, &parsed, *count);

            if (rc != 0) {
                return rc;
            }

            (void)senml_json_skip_ws(buf, len, &pos);

            if (pos < len && buf[pos] == ',') {
                pos++;
                continue;
            }

            if (pos < len && buf[pos] == ']') {
                pos++;
                break;
            }

            return -EINVAL;
        }
    } else if (buf[pos] == '{') {
        bool has_entries = false;

        pos++;

        for (;;) {
            char key[8];
            int key_length;

            (void)senml_json_skip_ws(buf, len, &pos);

            if (pos >= len) {
                return -EINVAL;
            }

            if (buf[pos] == '}') {
                pos++;
                break;
            }

            key_length = senml_json_read_string(buf, len, &pos, key, sizeof(key));

            if (key_length < 0) {
                return key_length;
            }

            (void)senml_json_skip_ws(buf, len, &pos);

            if (pos >= len || buf[pos] != ':') {
                return -EINVAL;
            }

            pos++;
            (void)senml_json_skip_ws(buf, len, &pos);

            if (pos >= len) {
                return -EINVAL;
            }

            if (strcmp(key, "bn") == 0) {
                int base_length = senml_json_read_string(buf, len, &pos, base, sizeof(base));

                if (base_length < 0) {
                    return base_length;
                }
            } else if (strcmp(key, "e") == 0) {
                if (buf[pos] != '[') {
                    return -EINVAL;
                }

                pos++;

                for (;;) {
                    int rc;

                    (void)senml_json_skip_ws(buf, len, &pos);

                    if (pos >= len) {
                        return -EINVAL;
                    }

                    if (buf[pos] == ']') {
                        pos++;
                        break;
                    }

                    rc = senml_json_record(buf, len, &pos, base, records, &parsed, *count);

                    if (rc != 0) {
                        return rc;
                    }

                    (void)senml_json_skip_ws(buf, len, &pos);

                    if (pos < len && buf[pos] == ',') {
                        pos++;
                        continue;
                    }

                    if (pos < len && buf[pos] == ']') {
                        pos++;
                        break;
                    }

                    return -EINVAL;
                }

                has_entries = true;
            } else {
                if (senml_json_skip_value(buf, len, &pos) != 0) {
                    return -EINVAL;
                }
            }

            (void)senml_json_skip_ws(buf, len, &pos);

            if (pos < len && buf[pos] == ',') {
                pos++;
                continue;
            }

            if (pos < len && buf[pos] == '}') {
                pos++;
                break;
            }

            return -EINVAL;
        }

        if (!has_entries) {
            return -EINVAL;
        }
    } else {
        return -EINVAL;
    }

    *count = parsed;

    return 0;
}

static int senml_cbor_skip_value(const uint8_t *buf, size_t len, size_t *pos, size_t depth)
{
    uint8_t major;
    uint64_t arg;

    if (depth >= SIRENA_LWM2M_COMPOSITE_MAX_DEPTH) {
        return -EINVAL;
    }

    if (read_cbor_head(buf, len, pos, &major, &arg) != 0) {
        return -EINVAL;
    }

    if (major == 0U || major == 1U) {
        return 0;
    }

    if (major == 2U || major == 3U) {
        if (arg > (uint64_t)(len - *pos)) {
            return -EINVAL;
        }

        *pos += (size_t)arg;

        return 0;
    }

    if (major == 4U) {
        uint64_t i;

        for (i = 0U; i < arg; i++) {
            if (i >= (uint64_t)(len - *pos)) {
                return -EINVAL;
            }

            if (senml_cbor_skip_value(buf, len, pos, depth + 1U) != 0) {
                return -EINVAL;
            }
        }

        return 0;
    }

    if (major == 5U) {
        uint64_t i;

        for (i = 0U; i < arg; i++) {
            if (i >= (uint64_t)(len - *pos)) {
                return -EINVAL;
            }

            if (senml_cbor_skip_value(buf, len, pos, depth + 1U) != 0) {
                return -EINVAL;
            }

            if (i >= (uint64_t)(len - *pos)) {
                return -EINVAL;
            }

            if (senml_cbor_skip_value(buf, len, pos, depth + 1U) != 0) {
                return -EINVAL;
            }
        }

        return 0;
    }

    return -ENOTSUP;
}

static int senml_cbor_text(const uint8_t *buf, size_t len, size_t *pos, char *out,
                           size_t out_size)
{
    uint8_t major;
    uint64_t arg;

    if (read_cbor_head(buf, len, pos, &major, &arg) != 0) {
        return -EINVAL;
    }

    if (major != 3U || arg == 0U || arg >= out_size) {
        return -EINVAL;
    }

    if (arg > (uint64_t)(len - *pos)) {
        return -EINVAL;
    }

    memcpy(out, buf + *pos, (size_t)arg);
    out[arg] = '\0';
    *pos += (size_t)arg;

    return (int)arg;
}

static int senml_cbor_value_text(const uint8_t *buf, size_t len, size_t *pos, char *out,
                                 size_t *value_length, bool *value_is_string)
{
    size_t start = *pos;
    uint8_t major;
    uint64_t arg;
    int written;

    if (read_cbor_head(buf, len, pos, &major, &arg) != 0) {
        return -EINVAL;
    }

    *value_is_string = false;

    if (major == 3U) {
        if (arg == 0U) {
            return -EINVAL;
        }

        if (arg > (uint64_t)(len - *pos)) {
            return -EINVAL;
        }

        if (arg >= SIRENA_LWM2M_COMPOSITE_VALUE_MAX) {
            *value_length = 0U;
            *value_is_string = true;
            out[0] = '\0';

            return -ENOSPC;
        }

        memcpy(out, buf + *pos, (size_t)arg);
        out[arg] = '\0';
        *pos += (size_t)arg;
        *value_length = (size_t)arg;
        *value_is_string = true;

        return 0;
    }

    if (major == 0U) {
        written = snprintf(out, SIRENA_LWM2M_COMPOSITE_VALUE_MAX, "%" PRIu64, arg);
    } else if (major == 1U) {
        if (arg == UINT64_MAX) {
            return -EINVAL;
        }

        written = snprintf(out, SIRENA_LWM2M_COMPOSITE_VALUE_MAX, "-%" PRIu64, arg + 1U);
    } else if (major == 7U && arg == 20U) {
        written = snprintf(out, SIRENA_LWM2M_COMPOSITE_VALUE_MAX, "false");
    } else if (major == 7U && arg == 21U) {
        written = snprintf(out, SIRENA_LWM2M_COMPOSITE_VALUE_MAX, "true");
    } else if (major == 7U && arg == 26U) {
        float number;
        uint32_t bits = 0U;
        size_t i;

        if (start + 5U > len) {
            return -EINVAL;
        }

        for (i = 0U; i < sizeof(bits); i++) {
            bits = (bits << 8) | (uint32_t)buf[start + 1U + i];
        }

        memcpy(&number, &bits, sizeof(number));
        written = snprintf(out, SIRENA_LWM2M_COMPOSITE_VALUE_MAX, "%g", (double)number);
    } else if (major == 7U && arg == 27U) {
        double number;
        uint64_t bits = 0U;
        size_t i;

        if (start + 9U > len) {
            return -EINVAL;
        }

        for (i = 0U; i < sizeof(bits); i++) {
            bits = (bits << 8) | (uint64_t)buf[start + 1U + i];
        }

        memcpy(&number, &bits, sizeof(number));
        written = snprintf(out, SIRENA_LWM2M_COMPOSITE_VALUE_MAX, "%g", number);
    } else {
        return -ENOTSUP;
    }

    if (written < 0) {
        return -EINVAL;
    }

    if ((size_t)written >= SIRENA_LWM2M_COMPOSITE_VALUE_MAX) {
        *value_length = 0U;
        out[0] = '\0';

        return -ENOSPC;
    }

    *value_length = (size_t)written;

    return 0;
}

static int senml_cbor_record(const uint8_t *buf, size_t len, size_t *pos, const char *pack_base,
                             struct sirena_lwm2m_senml_record *records, size_t *parsed,
                             size_t capacity)
{
    char base[SIRENA_LWM2M_COMPOSITE_PATH_MAX];
    char name[SIRENA_LWM2M_COMPOSITE_PATH_MAX];
    char value[SIRENA_LWM2M_COMPOSITE_VALUE_MAX];
    size_t value_length = 0U;
    bool value_is_string = false;
    bool value_invalid = false;
    bool has_name = false;
    bool has_value = false;
    uint8_t major;
    uint64_t arg;
    uint64_t member;

    strcpy(base, pack_base);

    if (read_cbor_head(buf, len, pos, &major, &arg) != 0) {
        return -EINVAL;
    }

    if (major != 5U) {
        return -EINVAL;
    }

    for (member = 0U; member < arg; member++) {
        char key[8];

        if (senml_cbor_text(buf, len, pos, key, sizeof(key)) < 0) {
            return -EINVAL;
        }

        if (strcmp(key, "bn") == 0) {
            int base_length = senml_cbor_text(buf, len, pos, base,
                                              SIRENA_LWM2M_COMPOSITE_PATH_MAX);

            if (base_length < 0) {
                return base_length;
            }
        } else if (strcmp(key, "n") == 0) {
            int name_length = senml_cbor_text(buf, len, pos, name, sizeof(name));

            if (name_length < 0) {
                return name_length;
            }

            has_name = true;
        } else if (strcmp(key, "v") == 0) {
            int rc = senml_cbor_value_text(buf, len, pos, value, &value_length, &value_is_string);

            if (rc == -ENOSPC) {
                value_invalid = true;
            } else if (rc != 0) {
                return rc;
            }

            has_value = true;
        } else {
            if (senml_cbor_skip_value(buf, len, pos, 0U) != 0) {
                return -EINVAL;
            }
        }
    }

    if (!has_name || !has_value) {
        return -EINVAL;
    }

    if (*parsed >= capacity) {
        return -ENOSPC;
    }

    if (name[0] == '/') {
        if (strlen(name) >= sizeof(records[*parsed].path)) {
            return -ENOSPC;
        }

        strcpy(records[*parsed].path, name);
    } else {
        size_t base_length = strlen(base);
        size_t name_length = strlen(name);

        if (base_length + name_length >= sizeof(records[*parsed].path)) {
            return -ENOSPC;
        }

        memcpy(records[*parsed].path, base, base_length);
        memcpy(records[*parsed].path + base_length, name, name_length + 1U);
    }

    memcpy(records[*parsed].value, value, value_length);
    records[*parsed].value[value_length] = '\0';
    records[*parsed].value_length = value_length;
    records[*parsed].value_is_string = value_is_string;
    records[*parsed].value_invalid = value_invalid;
    (*parsed)++;

    return 0;
}

static int senml_cbor_records(const uint8_t *buf, size_t len,
                               struct sirena_lwm2m_senml_record *records, size_t *count)
{
    size_t pos = 0U;
    size_t parsed = 0U;
    char base[SIRENA_LWM2M_COMPOSITE_PATH_MAX];
    uint8_t major;
    uint64_t arg;

    base[0] = '\0';

    if (read_cbor_head(buf, len, &pos, &major, &arg) != 0) {
        return -EINVAL;
    }

    if (major == 4U) {
        uint64_t item;

        for (item = 0U; item < arg; item++) {
            int rc = senml_cbor_record(buf, len, &pos, base, records, &parsed, *count);

            if (rc != 0) {
                return rc;
            }
        }
    } else if (major == 5U) {
        uint64_t member;
        uint64_t members = arg;

        for (member = 0U; member < members; member++) {
            char key[8];

            if (senml_cbor_text(buf, len, &pos, key, sizeof(key)) < 0) {
                return -EINVAL;
            }

            if (strcmp(key, "bn") == 0) {
                int base_length = senml_cbor_text(buf, len, &pos, base, sizeof(base));

                if (base_length < 0) {
                    return base_length;
                }
            } else if (strcmp(key, "e") == 0) {
                uint8_t items_major;
                uint64_t items;
                uint64_t item;

                if (read_cbor_head(buf, len, &pos, &items_major, &items) != 0) {
                    return -EINVAL;
                }

                if (items_major != 4U) {
                    return -EINVAL;
                }

                for (item = 0U; item < items; item++) {
                    int rc = senml_cbor_record(buf, len, &pos, base, records, &parsed, *count);

                    if (rc != 0) {
                        return rc;
                    }
                }
            } else {
                if (senml_cbor_skip_value(buf, len, &pos, 0U) != 0) {
                    return -EINVAL;
                }
            }
        }
    } else {
        return -EINVAL;
    }

    *count = parsed;

    return 0;
}

int sirena_lwm2m_codec_decode_senml_records(uint16_t content_format, const void *buffer,
                                             size_t length,
                                             struct sirena_lwm2m_senml_record *records,
                                             size_t *count)
{
    if (buffer == NULL || records == NULL || count == NULL || *count == 0U) {
        return -EINVAL;
    }

    if (content_format == SIRENA_LWM2M_CF_SENML_JSON) {
        return senml_json_records((const char *)buffer, length, records, count);
    }

    if (content_format == SIRENA_LWM2M_CF_SENML_CBOR) {
        return senml_cbor_records((const uint8_t *)buffer, length, records, count);
    }

    return -ENOTSUP;
}
