#include "sirena_lwm2m_mqtt_payload.h"
#include <errno.h>
#include <string.h>

#define SIRENA_LWM2M_CBOR_MAJOR_UINT 0U
#define SIRENA_LWM2M_CBOR_MAJOR_BYTES 2U
#define SIRENA_LWM2M_CBOR_MAJOR_TEXT 3U
#define SIRENA_LWM2M_CBOR_MAJOR_ARRAY 4U
#define SIRENA_LWM2M_CBOR_MAJOR_MAP 5U
#define SIRENA_LWM2M_CBOR_MAJOR_TAG 6U
#define SIRENA_LWM2M_CBOR_MAJOR_SIMPLE 7U

#define SIRENA_LWM2M_CBOR_KEY_OPERATION 1U
#define SIRENA_LWM2M_CBOR_KEY_TOKEN 2U
#define SIRENA_LWM2M_CBOR_KEY_ENDPOINT 3U
#define SIRENA_LWM2M_CBOR_KEY_URI 5U
#define SIRENA_LWM2M_CBOR_KEY_PAYLOAD 7U
#define SIRENA_LWM2M_CBOR_KEY_LIFETIME 8U
#define SIRENA_LWM2M_CBOR_KEY_VERSION 9U
#define SIRENA_LWM2M_CBOR_KEY_BINDING 10U
#define SIRENA_LWM2M_CBOR_KEY_RESULT 18U
#define SIRENA_LWM2M_CBOR_KEY_CONTENT_FORMAT 19U

#define SIRENA_LWM2M_CBOR_MAX_DEPTH 8U

struct sirena_lwm2m_cbor_writer {
    uint8_t *buffer;
    size_t capacity;
    size_t length;
};

struct sirena_lwm2m_cbor_reader {
    const uint8_t *buffer;
    size_t length;
    size_t offset;
};

static int cbor_write(struct sirena_lwm2m_cbor_writer *writer, const void *data, size_t length)
{
    if (writer->length + length > writer->capacity) {
        return -ENOMEM;
    }

    memcpy(writer->buffer + writer->length, data, length);
    writer->length += length;

    return 0;
}

static int cbor_write_byte(struct sirena_lwm2m_cbor_writer *writer, uint8_t value)
{
    return cbor_write(writer, &value, sizeof(value));
}

static int cbor_write_head(struct sirena_lwm2m_cbor_writer *writer, uint8_t major, uint64_t value)
{
    uint8_t head = (uint8_t)(major << 5U);
    int rc;

    if (value <= 23U) {
        return cbor_write_byte(writer, (uint8_t)(head | (uint8_t)value));
    }

    if (value <= 0xFFU) {
        rc = cbor_write_byte(writer, (uint8_t)(head | 24U));
        if (rc != 0) {
            return rc;
        }

        return cbor_write_byte(writer, (uint8_t)value);
    }

    if (value <= 0xFFFFU) {
        uint8_t bytes[2] = { (uint8_t)(value >> 8U), (uint8_t)value };

        rc = cbor_write_byte(writer, (uint8_t)(head | 25U));
        if (rc != 0) {
            return rc;
        }

        return cbor_write(writer, bytes, sizeof(bytes));
    }

    if (value <= 0xFFFFFFFFU) {
        uint8_t bytes[4];
        unsigned int i;

        for (i = 0U; i < 4U; ++i) {
            bytes[3U - i] = (uint8_t)(value >> (i * 8U));
        }

        rc = cbor_write_byte(writer, (uint8_t)(head | 26U));
        if (rc != 0) {
            return rc;
        }

        return cbor_write(writer, bytes, sizeof(bytes));
    }

    {
        uint8_t bytes[8];
        unsigned int i;

        for (i = 0U; i < 8U; ++i) {
            bytes[7U - i] = (uint8_t)(value >> (i * 8U));
        }

        rc = cbor_write_byte(writer, (uint8_t)(head | 27U));
        if (rc != 0) {
            return rc;
        }

        return cbor_write(writer, bytes, sizeof(bytes));
    }
}

static int cbor_write_uint(struct sirena_lwm2m_cbor_writer *writer, uint64_t value)
{
    return cbor_write_head(writer, SIRENA_LWM2M_CBOR_MAJOR_UINT, value);
}

static int cbor_write_text(struct sirena_lwm2m_cbor_writer *writer, const char *text,
                            size_t length)
{
    int rc;

    rc = cbor_write_head(writer, SIRENA_LWM2M_CBOR_MAJOR_TEXT, length);
    if (rc != 0) {
        return rc;
    }

    if (length == 0U) {
        return 0;
    }

    return cbor_write(writer, text, length);
}

static int cbor_write_bytes(struct sirena_lwm2m_cbor_writer *writer, const uint8_t *data,
                             size_t length)
{
    int rc;

    rc = cbor_write_head(writer, SIRENA_LWM2M_CBOR_MAJOR_BYTES, length);
    if (rc != 0) {
        return rc;
    }

    if (length == 0U) {
        return 0;
    }

    return cbor_write(writer, data, length);
}

static int cbor_read_head(struct sirena_lwm2m_cbor_reader *reader, uint8_t *major,
                          uint64_t *value)
{
    uint8_t head;
    uint8_t additional;
    uint64_t result;
    unsigned int i;

    if (reader->offset >= reader->length) {
        return -EINVAL;
    }

    head = reader->buffer[reader->offset++];
    *major = (uint8_t)(head >> 5U);
    additional = (uint8_t)(head & 0x1FU);

    if (additional <= 23U) {
        *value = additional;
        return 0;
    }

    if (additional == 24U) {
        if (reader->offset + 1U > reader->length) {
            return -EINVAL;
        }

        *value = reader->buffer[reader->offset++];
        return 0;
    }

    if (additional == 25U) {
        if (reader->offset + 2U > reader->length) {
            return -EINVAL;
        }

        result = 0U;
        for (i = 0U; i < 2U; ++i) {
            result = (result << 8U) | reader->buffer[reader->offset++];
        }

        *value = result;
        return 0;
    }

    if (additional == 26U) {
        if (reader->offset + 4U > reader->length) {
            return -EINVAL;
        }

        result = 0U;
        for (i = 0U; i < 4U; ++i) {
            result = (result << 8U) | reader->buffer[reader->offset++];
        }

        *value = result;
        return 0;
    }

    if (additional == 27U) {
        if (reader->offset + 8U > reader->length) {
            return -EINVAL;
        }

        result = 0U;
        for (i = 0U; i < 8U; ++i) {
            result = (result << 8U) | reader->buffer[reader->offset++];
        }

        *value = result;
        return 0;
    }

    return -EINVAL;
}

static int cbor_skip(struct sirena_lwm2m_cbor_reader *reader, uint8_t major, uint64_t value,
                      unsigned int depth)
{
    uint64_t count;
    uint64_t i;
    uint8_t nested_major;
    uint64_t nested_value;
    int rc;

    switch (major) {
    case SIRENA_LWM2M_CBOR_MAJOR_UINT:
    case SIRENA_LWM2M_CBOR_MAJOR_SIMPLE:
        return 0;
    case SIRENA_LWM2M_CBOR_MAJOR_BYTES:
    case SIRENA_LWM2M_CBOR_MAJOR_TEXT:
        if (value > reader->length - reader->offset) {
            return -EINVAL;
        }

        reader->offset += (size_t)value;
        return 0;
    case SIRENA_LWM2M_CBOR_MAJOR_ARRAY:
    case SIRENA_LWM2M_CBOR_MAJOR_MAP:
        if (depth >= SIRENA_LWM2M_CBOR_MAX_DEPTH) {
            return -EINVAL;
        }

        count = value;
        if (major == SIRENA_LWM2M_CBOR_MAJOR_MAP) {
            count *= 2U;
        }

        for (i = 0U; i < count; ++i) {
            rc = cbor_read_head(reader, &nested_major, &nested_value);
            if (rc != 0) {
                return rc;
            }

            rc = cbor_skip(reader, nested_major, nested_value, depth + 1U);
            if (rc != 0) {
                return rc;
            }
        }

        return 0;
    case SIRENA_LWM2M_CBOR_MAJOR_TAG:
        if (depth >= SIRENA_LWM2M_CBOR_MAX_DEPTH) {
            return -EINVAL;
        }

        rc = cbor_read_head(reader, &nested_major, &nested_value);
        if (rc != 0) {
            return rc;
        }

        return cbor_skip(reader, nested_major, nested_value, depth + 1U);
    default:
        return -EINVAL;
    }
}

static int cbor_read_text(struct sirena_lwm2m_cbor_reader *reader, char *text, size_t capacity,
                           size_t *length)
{
    uint8_t major;
    uint64_t value;
    int rc;

    rc = cbor_read_head(reader, &major, &value);
    if (rc != 0) {
        return rc;
    }

    if (major != SIRENA_LWM2M_CBOR_MAJOR_TEXT) {
        return -EINVAL;
    }

    if (value > SIRENA_LWM2M_MQTT_TEXT_MAX || value > reader->length - reader->offset) {
        return -EINVAL;
    }

    memcpy(text, reader->buffer + reader->offset, (size_t)value);
    text[value] = '\0';
    reader->offset += (size_t)value;
    *length = (size_t)value;

    return 0;
}

static int cbor_read_bytes(struct sirena_lwm2m_cbor_reader *reader, const uint8_t **data,
                            size_t *length)
{
    uint8_t major;
    uint64_t value;
    int rc;

    rc = cbor_read_head(reader, &major, &value);
    if (rc != 0) {
        return rc;
    }

    if (major != SIRENA_LWM2M_CBOR_MAJOR_BYTES) {
        return -EINVAL;
    }

    if (value > reader->length - reader->offset) {
        return -EINVAL;
    }

    *data = reader->buffer + reader->offset;
    *length = (size_t)value;
    reader->offset += (size_t)value;

    return 0;
}

static int cbor_read_uint(struct sirena_lwm2m_cbor_reader *reader, uint32_t *value)
{
    uint8_t major;
    uint64_t result;
    int rc;

    rc = cbor_read_head(reader, &major, &result);
    if (rc != 0) {
        return rc;
    }

    if (major != SIRENA_LWM2M_CBOR_MAJOR_UINT || result > UINT32_MAX) {
        return -EINVAL;
    }

    *value = (uint32_t)result;

    return 0;
}

static size_t message_entry_count(const struct sirena_lwm2m_mqtt_message *message)
{
    size_t count = 0U;

    count += message->has_operation ? 1U : 0U;
    count += 1U;
    count += message->has_endpoint ? 1U : 0U;
    count += message->has_uri ? 1U : 0U;
    count += message->has_payload ? 1U : 0U;
    count += message->has_lifetime ? 1U : 0U;
    count += message->has_version ? 1U : 0U;
    count += message->has_binding ? 1U : 0U;
    count += message->has_result ? 1U : 0U;
    count += message->has_content_format ? 1U : 0U;

    return count;
}

int sirena_lwm2m_mqtt_message_encode(const struct sirena_lwm2m_mqtt_message *message,
                                     void *buffer, size_t *length)
{
    struct sirena_lwm2m_cbor_writer writer = {
        .buffer = buffer,
        .capacity = 0U,
        .length = 0U,
    };
    size_t entries;
    int rc;

    if (message == NULL || buffer == NULL || length == NULL) {
        return -EINVAL;
    }

    writer.capacity = *length;
    writer.length = 0U;

    entries = message_entry_count(message);

    rc = cbor_write_head(&writer, SIRENA_LWM2M_CBOR_MAJOR_MAP, entries);
    if (rc != 0) {
        return rc;
    }

    if (message->has_operation) {
        rc = cbor_write_uint(&writer, SIRENA_LWM2M_CBOR_KEY_OPERATION);
        if (rc != 0) {
            return rc;
        }

        rc = cbor_write_uint(&writer, message->operation);
        if (rc != 0) {
            return rc;
        }
    }

    rc = cbor_write_uint(&writer, SIRENA_LWM2M_CBOR_KEY_TOKEN);
    if (rc != 0) {
        return rc;
    }

    rc = cbor_write_uint(&writer, message->token);
    if (rc != 0) {
        return rc;
    }

    if (message->has_endpoint) {
        rc = cbor_write_uint(&writer, SIRENA_LWM2M_CBOR_KEY_ENDPOINT);
        if (rc != 0) {
            return rc;
        }

        rc = cbor_write_text(&writer, message->endpoint, strlen(message->endpoint));
        if (rc != 0) {
            return rc;
        }
    }

    if (message->has_uri) {
        rc = cbor_write_uint(&writer, SIRENA_LWM2M_CBOR_KEY_URI);
        if (rc != 0) {
            return rc;
        }

        rc = cbor_write_text(&writer, message->uri, strlen(message->uri));
        if (rc != 0) {
            return rc;
        }
    }

    if (message->has_payload) {
        rc = cbor_write_uint(&writer, SIRENA_LWM2M_CBOR_KEY_PAYLOAD);
        if (rc != 0) {
            return rc;
        }

        rc = cbor_write_bytes(&writer, message->payload, message->payload_length);
        if (rc != 0) {
            return rc;
        }
    }

    if (message->has_lifetime) {
        rc = cbor_write_uint(&writer, SIRENA_LWM2M_CBOR_KEY_LIFETIME);
        if (rc != 0) {
            return rc;
        }

        rc = cbor_write_uint(&writer, message->lifetime);
        if (rc != 0) {
            return rc;
        }
    }

    if (message->has_version) {
        rc = cbor_write_uint(&writer, SIRENA_LWM2M_CBOR_KEY_VERSION);
        if (rc != 0) {
            return rc;
        }

        rc = cbor_write_text(&writer, message->version, strlen(message->version));
        if (rc != 0) {
            return rc;
        }
    }

    if (message->has_binding) {
        rc = cbor_write_uint(&writer, SIRENA_LWM2M_CBOR_KEY_BINDING);
        if (rc != 0) {
            return rc;
        }

        rc = cbor_write_text(&writer, message->binding, strlen(message->binding));
        if (rc != 0) {
            return rc;
        }
    }

    if (message->has_result) {
        rc = cbor_write_uint(&writer, SIRENA_LWM2M_CBOR_KEY_RESULT);
        if (rc != 0) {
            return rc;
        }

        rc = cbor_write_uint(&writer, message->result);
        if (rc != 0) {
            return rc;
        }
    }

    if (message->has_content_format) {
        rc = cbor_write_uint(&writer, SIRENA_LWM2M_CBOR_KEY_CONTENT_FORMAT);
        if (rc != 0) {
            return rc;
        }

        rc = cbor_write_uint(&writer, message->content_format);
        if (rc != 0) {
            return rc;
        }
    }

    *length = writer.length;

    return 0;
}

int sirena_lwm2m_mqtt_message_decode(const void *data, size_t length,
                                     struct sirena_lwm2m_mqtt_message *message)
{
    struct sirena_lwm2m_cbor_reader reader = {
        .buffer = data,
        .length = length,
        .offset = 0U,
    };
    uint8_t major;
    uint64_t entries;
    uint64_t i;
    int rc;

    if (data == NULL || message == NULL) {
        return -EINVAL;
    }

    memset(message, 0, sizeof(*message));

    rc = cbor_read_head(&reader, &major, &entries);
    if (rc != 0) {
        return rc;
    }

    if (major != SIRENA_LWM2M_CBOR_MAJOR_MAP) {
        return -EINVAL;
    }

    for (i = 0U; i < entries; ++i) {
        uint32_t key;
        uint8_t value_major;
        uint64_t value;
        size_t text_length = 0U;

        rc = cbor_read_uint(&reader, &key);
        if (rc != 0) {
            return rc;
        }

        switch (key) {
        case SIRENA_LWM2M_CBOR_KEY_OPERATION:
            rc = cbor_read_uint(&reader, &message->operation);
            message->has_operation = rc == 0;
            break;
        case SIRENA_LWM2M_CBOR_KEY_TOKEN:
            rc = cbor_read_uint(&reader, &message->token);
            break;
        case SIRENA_LWM2M_CBOR_KEY_LIFETIME:
            rc = cbor_read_uint(&reader, &message->lifetime);
            message->has_lifetime = true;
            break;
        case SIRENA_LWM2M_CBOR_KEY_RESULT:
            rc = cbor_read_uint(&reader, &message->result);
            message->has_result = true;
            break;
        case SIRENA_LWM2M_CBOR_KEY_CONTENT_FORMAT:
            rc = cbor_read_uint(&reader, &message->content_format);
            message->has_content_format = true;
            break;
        case SIRENA_LWM2M_CBOR_KEY_ENDPOINT:
            rc = cbor_read_text(&reader, message->endpoint, sizeof(message->endpoint),
                                &text_length);
            message->has_endpoint = rc == 0;
            break;
        case SIRENA_LWM2M_CBOR_KEY_VERSION:
            rc = cbor_read_text(&reader, message->version, sizeof(message->version),
                                &text_length);
            message->has_version = rc == 0;
            break;
        case SIRENA_LWM2M_CBOR_KEY_BINDING:
            rc = cbor_read_text(&reader, message->binding, sizeof(message->binding),
                                &text_length);
            message->has_binding = rc == 0;
            break;
        case SIRENA_LWM2M_CBOR_KEY_URI:
            rc = cbor_read_text(&reader, message->uri, sizeof(message->uri), &text_length);
            message->has_uri = rc == 0;
            break;
        case SIRENA_LWM2M_CBOR_KEY_PAYLOAD:
            rc = cbor_read_bytes(&reader, &message->payload, &message->payload_length);
            message->has_payload = true;
            break;
        default:
            rc = cbor_read_head(&reader, &value_major, &value);
            if (rc == 0) {
                rc = cbor_skip(&reader, value_major, value, 0U);
            }
            break;
        }

        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}
