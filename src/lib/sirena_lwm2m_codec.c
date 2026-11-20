#include <sirena/lwm2m/codec.h>
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

static bool supported(uint16_t format)
{
    switch (format) {
    case SIRENA_LWM2M_CF_PLAIN_TEXT:
    case SIRENA_LWM2M_CF_TLV:
    case SIRENA_LWM2M_CF_JSON:
    case SIRENA_LWM2M_CF_CBOR:
    case SIRENA_LWM2M_CF_SENML_JSON:
    case SIRENA_LWM2M_CF_SENML_CBOR:
    case SIRENA_LWM2M_CF_SENML_ETCH_JSON:
    case SIRENA_LWM2M_CF_SENML_ETCH_CBOR:
    case SIRENA_LWM2M_CF_OPAQUE:
        return true;
    default:
        return false;
    }
}

bool sirena_lwm2m_content_format_supported(uint16_t format)
{
    return supported(format);
}

int sirena_lwm2m_content_format_from_mime(const char *mime, uint16_t *format)
{
    if (mime == NULL || format == NULL) {
        return -EINVAL;
    }
    if (strcmp(mime, "text/plain") == 0) {
        *format = SIRENA_LWM2M_CF_PLAIN_TEXT;
        return 0;
    }
    if (strcmp(mime, "application/vnd.oma.lwm2m+tlv") == 0) {
        *format = SIRENA_LWM2M_CF_TLV;
        return 0;
    }
    if (strcmp(mime, "application/vnd.oma.lwm2m+json") == 0) {
        *format = SIRENA_LWM2M_CF_JSON;
        return 0;
    }
    if (strcmp(mime, "application/vnd.oma.lwm2m+cbor") == 0) {
        *format = SIRENA_LWM2M_CF_CBOR;
        return 0;
    }
    if (strcmp(mime, "application/senml+json") == 0) {
        *format = SIRENA_LWM2M_CF_SENML_JSON;
        return 0;
    }
    if (strcmp(mime, "application/senml+cbor") == 0) {
        *format = SIRENA_LWM2M_CF_SENML_CBOR;
        return 0;
    }
    if (strcmp(mime, "application/senml-etch+json") == 0) {
        *format = SIRENA_LWM2M_CF_SENML_ETCH_JSON;
        return 0;
    }
    if (strcmp(mime, "application/senml-etch+cbor") == 0) {
        *format = SIRENA_LWM2M_CF_SENML_ETCH_CBOR;
        return 0;
    }
    if (strcmp(mime, "application/octet-stream") == 0) {
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

uint16_t sirena_lwm2m_content_format_negotiate(const uint16_t *accepted,
                                               size_t count,
                                               uint16_t preferred)
{
    size_t i;

    if (preferred != 0 && supported(preferred)) {
        if (accepted == NULL || count == 0U) {
            return preferred;
        }
        for (i = 0U; i < count; ++i) {
            if (accepted[i] == preferred) {
                return preferred;
            }
        }
    }
    if (accepted != NULL) {
        for (i = 0U; i < count; ++i) {
            if (supported(accepted[i])) {
                return accepted[i];
            }
        }
    }
    return 0;
}

static int encode_tlv_value(void *buffer, size_t capacity, size_t *used,
                           const struct sirena_lwm2m_codec_value *v)
{
    uint8_t type_byte = 0x80;
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
        value_len = 4U;
        for (size_t i = 0U; i < 4U; ++i) {
            tmp[3U - i] = (uint8_t)((v->data.integer >> (i * 8U)) & 0xFF);
        }
        break;
    case SIRENA_LWM2M_VALUE_UNSIGNED:
        value_len = 4U;
        for (size_t i = 0U; i < 4U; ++i) {
            tmp[3U - i] = (uint8_t)((v->data.unsigned_integer >> (i * 8U)) & 0xFF);
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

static int encode_json_value(char *buffer, size_t capacity, size_t *used,
                            const struct sirena_lwm2m_codec_value *v,
                            bool comma)
{
    char tmp[128];
    int n;
    if (comma) {
        if (append_byte(buffer, capacity, used, ',') != 0) return -ENOMEM;
    }
    n = snprintf(tmp, sizeof(tmp), "\"%u\":", v->id);
    if (n < 0) return -EINVAL;
    if (append(buffer, capacity, used, tmp, (size_t)n) != 0) return -ENOMEM;

    switch (v->type) {
    case SIRENA_LWM2M_VALUE_BOOLEAN:
        {
            const char *s = v->data.boolean ? "true" : "false";
            return append(buffer, capacity, used, s, strlen(s));
        }
    case SIRENA_LWM2M_VALUE_INTEGER:
        n = snprintf(tmp, sizeof(tmp), "%ld", (long)v->data.integer);
        break;
    case SIRENA_LWM2M_VALUE_UNSIGNED:
        n = snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)v->data.unsigned_integer);
        break;
    case SIRENA_LWM2M_VALUE_FLOAT:
        n = snprintf(tmp, sizeof(tmp), "%g", v->data.floating_point);
        break;
    case SIRENA_LWM2M_VALUE_STRING: {
        if (append_byte(buffer, capacity, used, '"') != 0) return -ENOMEM;
        int r = append(buffer, capacity, used, v->data.string.value,
                       v->data.string.length);
        if (r != 0) return r;
        return append_byte(buffer, capacity, used, '"');
    }
    case SIRENA_LWM2M_VALUE_OPAQUE: {
        if (append_byte(buffer, capacity, used, '"') != 0) return -ENOMEM;
        int r = append(buffer, capacity, used, v->data.opaque.value,
                       v->data.opaque.length);
        if (r != 0) return r;
        return append_byte(buffer, capacity, used, '"');
    }
    default:
        return -EINVAL;
    }
    if (n < 0) return -EINVAL;
    return append(buffer, capacity, used, tmp, (size_t)n);
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
    if (values == NULL && value_count != 0U) {
        return -EINVAL;
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
            uint8_t rec = 0xA0 | 2U;
            char rid[16];
            snprintf(rid, sizeof(rid), "%u", effective_resource_id(&values[i]));
            char path[64];
            snprintf(path, sizeof(path), "/%u/%u/%u", values[i].object_id, values[i].instance_id, effective_resource_id(&values[i]));
            rc = append_byte(buffer, cap, &used, rec);
            if (rc != 0) return rc;
            rc = cbor_length(buffer, cap, &used, 3, 1);
            if (rc != 0) return rc;
            rc = append(buffer, cap, &used, "v", 1U);
            if (rc != 0) return rc;
            rc = encode_cbor_value(buffer, cap, &used, &values[i]);
            if (rc != 0) return rc;
            rc = cbor_length(buffer, cap, &used, 3, 2);
            if (rc != 0) return rc;
            rc = append(buffer, cap, &used, "vb", 2U);
            if (rc != 0) return rc;
            (void)rid; (void)path;
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
        if (append(buffer, cap, &used, "{\"e\":[", 6U) != 0) return -ENOMEM;
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
            if (values[i].type == SIRENA_LWM2M_VALUE_INTEGER) {
                n = snprintf(tmp, sizeof(tmp), "%" PRId64, values[i].data.integer);
                if (n < 0 || append(buffer, cap, &used, tmp, (size_t)n) != 0) return -ENOMEM;
            } else {
                if (append(buffer, cap, &used, "null", 4U) != 0) return -ENOMEM;
            }
            if (append(buffer, cap, &used, ",\"vb\":true}", 11U) != 0) return -ENOMEM;
        }
        if (append(buffer, cap, &used, "]}", 2U) != 0) return -ENOMEM;
        *length = used;
        return 0;
    }

    return -ENOTSUP;
}

static int decode_tlv_value(const uint8_t *buf, size_t len,
                           struct sirena_lwm2m_codec_value *v,
                           size_t *consumed)
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
    id_bytes = (type_byte & 0x60) ? 2U : 1U;
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
    v->id = id;
    switch (value_len) {
    case 1U:
        v->type = SIRENA_LWM2M_VALUE_INTEGER;
        v->data.integer = buf[value_start];
        break;
    case 4U:
        v->type = SIRENA_LWM2M_VALUE_INTEGER;
        v->data.integer = ((int32_t)buf[value_start] << 24) |
                          ((int32_t)buf[value_start + 1] << 16) |
                          ((int32_t)buf[value_start + 2] << 8) |
                          (int32_t)buf[value_start + 3];
        break;
    case 8U: {
        uint64_t bits = 0U;
        v->type = SIRENA_LWM2M_VALUE_FLOAT;
        for (size_t i = 0U; i < 8U; ++i) {
            bits = (bits << 8) | buf[value_start + i];
        }
        memcpy(&v->data.floating_point, &bits, 8U);
        break;
    }
    default:
        v->type = SIRENA_LWM2M_VALUE_OPAQUE;
        v->data.opaque.value = &buf[value_start];
        v->data.opaque.length = value_len;
        break;
    }
    return 0;
}

int sirena_lwm2m_codec_decode(uint16_t content_format, const void *buffer,
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
    if (!supported(content_format)) {
        return -ENOTSUP;
    }

    if (content_format == SIRENA_LWM2M_CF_PLAIN_TEXT) {
        if (cap < 1U) return -ENOMEM;
        values[0].id = 0U;
        values[0].type = SIRENA_LWM2M_VALUE_STRING;
        values[0].data.string.value = buffer;
        values[0].data.string.length = length;
        *value_count = 1U;
        return 0;
    }

    if (content_format == SIRENA_LWM2M_CF_OPAQUE) {
        if (cap < 1U) return -ENOMEM;
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
                                &consumed) != 0) {
                return -EINVAL;
            }
            count += 1U;
            cursor += consumed;
        }
        *value_count = count;
        return 0;
    }

    if (content_format == SIRENA_LWM2M_CF_CBOR ||
        content_format == SIRENA_LWM2M_CF_SENML_CBOR ||
        content_format == SIRENA_LWM2M_CF_SENML_ETCH_CBOR) {
        if (cap < 1U) return -ENOMEM;
        values[0].id = 0U;
        values[0].type = SIRENA_LWM2M_VALUE_OPAQUE;
        values[0].data.opaque.value = buffer;
        values[0].data.opaque.length = length;
        *value_count = 1U;
        return 0;
    }

    if (content_format == SIRENA_LWM2M_CF_JSON ||
        content_format == SIRENA_LWM2M_CF_SENML_JSON ||
        content_format == SIRENA_LWM2M_CF_SENML_ETCH_JSON) {
        if (cap < 1U) return -ENOMEM;
        values[0].id = 0U;
        values[0].type = SIRENA_LWM2M_VALUE_OPAQUE;
        values[0].data.opaque.value = buffer;
        values[0].data.opaque.length = length;
        *value_count = 1U;
        return 0;
    }

    return -ENOTSUP;
}
