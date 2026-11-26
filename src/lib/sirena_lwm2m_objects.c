#include <sirena/lwm2m/objects.h>
#include <sirena/lwm2m/engine.h>
#include <sirena/lwm2m/object_registry.h>
#include <zephyr/sys/util.h>
#include <string.h>
#include <errno.h>

#define SIRENA_LWM2M_SECURITY_OBJECT_ID 0U
#define SIRENA_LWM2M_SERVER_OBJECT_ID 1U
#define SIRENA_LWM2M_DEVICE_OBJECT_ID 3U
#define SIRENA_LWM2M_SECURITY_URI_RESOURCE 0U
#define SIRENA_LWM2M_SECURITY_MODE_RESOURCE 2U
#define SIRENA_LWM2M_SECURITY_IDENTITY_RESOURCE 3U
#define SIRENA_LWM2M_SECURITY_SERVER_PUBLIC_KEY_RESOURCE 4U
#define SIRENA_LWM2M_SECURITY_SECRET_KEY_RESOURCE 5U
#define SIRENA_LWM2M_SECURITY_CLIENT_CERTIFICATE_RESOURCE SIRENA_LWM2M_SECURITY_IDENTITY_RESOURCE
#define SIRENA_LWM2M_SECURITY_TRUST_ANCHOR_RESOURCE SIRENA_LWM2M_SECURITY_SERVER_PUBLIC_KEY_RESOURCE
#define SIRENA_LWM2M_SECURITY_PRIVATE_KEY_RESOURCE SIRENA_LWM2M_SECURITY_SECRET_KEY_RESOURCE
#define SIRENA_LWM2M_SECURITY_SHORT_SERVER_ID_RESOURCE 10U

enum sirena_lwm2m_resource_kind {
    SIRENA_LWM2M_RESOURCE_VALUE,
    SIRENA_LWM2M_RESOURCE_CONSTANT,
    SIRENA_LWM2M_RESOURCE_EXECUTE
};

struct sirena_lwm2m_resource_entry {
    uint16_t id;
    uint8_t operations;
    bool kconfig_gated;
    enum sirena_lwm2m_value_type type;
    enum sirena_lwm2m_resource_kind kind;
    uint8_t *data;
    uint16_t capacity;
    uint16_t length;
    int64_t integer;
    uint64_t unsigned_integer;
    bool boolean;
    uint16_t object_link_object_id;
    uint16_t object_link_instance_id;
};

struct sirena_lwm2m_object_definition {
    uint16_t object_id;
    struct sirena_lwm2m_resource_entry *resources;
    size_t resource_count;
};

struct sirena_lwm2m_security_instance {
    char server_uri[CONFIG_SIRENA_LWM2M_SERVER_URI_MAX + 1U];
    uint8_t identity[CONFIG_SIRENA_LWM2M_CERTIFICATE_MAX];
    uint8_t server_public_key[CONFIG_SIRENA_LWM2M_CERTIFICATE_MAX];
    uint8_t secret_key[CONFIG_SIRENA_LWM2M_CERTIFICATE_MAX];
#if IS_ENABLED(CONFIG_SIRENA_LWM2M_OBJECT_SECURITY_OPTIONAL)
    uint8_t sms_binding_key_parameters[CONFIG_SIRENA_LWM2M_CREDENTIAL_MAX];
    uint8_t sms_binding_secret_key[CONFIG_SIRENA_LWM2M_CREDENTIAL_MAX];
    char server_sms_number[CONFIG_SIRENA_LWM2M_SERVER_STRING_MAX + 1U];
    char sni[CONFIG_SIRENA_LWM2M_SERVER_STRING_MAX + 1U];
    char rds_application_id[CONFIG_SIRENA_LWM2M_SERVER_STRING_MAX + 1U];
#endif
};

struct sirena_lwm2m_server_instance {
    char binding[CONFIG_SIRENA_LWM2M_SERVER_STRING_MAX + 1U];
};

struct sirena_lwm2m_device_instance {
    char manufacturer[CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX + 1U];
    char model_number[CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX + 1U];
    char serial_number[CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX + 1U];
    char firmware_version[CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX + 1U];
    char supported_binding[CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX + 1U];
    char device_type[CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX + 1U];
    char hardware_version[CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX + 1U];
    char software_version[CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX + 1U];
#if IS_ENABLED(CONFIG_SIRENA_LWM2M_OBJECT_DEVICE_OPTIONAL)
    char utc_offset[CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX + 1U];
    char timezone[CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX + 1U];
#endif
};

static struct sirena_lwm2m_security_instance security_instance;
static struct sirena_lwm2m_server_instance server_instance;
static struct sirena_lwm2m_device_instance device_instance;

static struct sirena_lwm2m_resource_entry security_resources[] = {
    { .id = 0U, .operations = SIRENA_LWM2M_OP_READ, .type = SIRENA_LWM2M_VALUE_STRING,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE, .data = security_instance.server_uri,
      .capacity = CONFIG_SIRENA_LWM2M_SERVER_URI_MAX },
    { .id = 1U, .operations = SIRENA_LWM2M_OP_READ, .type = SIRENA_LWM2M_VALUE_BOOLEAN,
      .kind = SIRENA_LWM2M_RESOURCE_CONSTANT },
    { .id = 2U, .operations = SIRENA_LWM2M_OP_READ, .type = SIRENA_LWM2M_VALUE_INTEGER,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = SIRENA_LWM2M_SECURITY_IDENTITY_RESOURCE, .operations = SIRENA_LWM2M_OP_READ, .type = SIRENA_LWM2M_VALUE_OPAQUE,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE, .data = security_instance.identity,
      .capacity = CONFIG_SIRENA_LWM2M_CERTIFICATE_MAX },
    { .id = SIRENA_LWM2M_SECURITY_SERVER_PUBLIC_KEY_RESOURCE, .operations = SIRENA_LWM2M_OP_READ, .type = SIRENA_LWM2M_VALUE_OPAQUE,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE, .data = security_instance.server_public_key,
      .capacity = CONFIG_SIRENA_LWM2M_CERTIFICATE_MAX },
    { .id = SIRENA_LWM2M_SECURITY_SECRET_KEY_RESOURCE, .operations = SIRENA_LWM2M_OP_READ, .type = SIRENA_LWM2M_VALUE_OPAQUE,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE, .data = security_instance.secret_key,
      .capacity = CONFIG_SIRENA_LWM2M_CERTIFICATE_MAX },
    { .id = SIRENA_LWM2M_SECURITY_SHORT_SERVER_ID_RESOURCE, .operations = SIRENA_LWM2M_OP_READ, .type = SIRENA_LWM2M_VALUE_INTEGER,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE },
#if IS_ENABLED(CONFIG_SIRENA_LWM2M_OBJECT_SECURITY_OPTIONAL)
    { .id = 6U, .operations = SIRENA_LWM2M_OP_READ, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_INTEGER, .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = 7U, .operations = SIRENA_LWM2M_OP_READ, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_OPAQUE, .kind = SIRENA_LWM2M_RESOURCE_VALUE,
      .data = security_instance.sms_binding_key_parameters,
      .capacity = CONFIG_SIRENA_LWM2M_CREDENTIAL_MAX },
    { .id = 8U, .operations = SIRENA_LWM2M_OP_READ, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_OPAQUE, .kind = SIRENA_LWM2M_RESOURCE_VALUE,
      .data = security_instance.sms_binding_secret_key,
      .capacity = CONFIG_SIRENA_LWM2M_CREDENTIAL_MAX },
    { .id = 9U, .operations = SIRENA_LWM2M_OP_READ, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_STRING, .kind = SIRENA_LWM2M_RESOURCE_VALUE,
      .data = security_instance.server_sms_number,
      .capacity = CONFIG_SIRENA_LWM2M_SERVER_STRING_MAX },
    { .id = 11U, .operations = SIRENA_LWM2M_OP_READ, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_INTEGER, .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = 12U, .operations = SIRENA_LWM2M_OP_READ, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_INTEGER, .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = 13U, .operations = SIRENA_LWM2M_OP_READ, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_UNSIGNED, .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = 14U, .operations = SIRENA_LWM2M_OP_READ, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_STRING, .kind = SIRENA_LWM2M_RESOURCE_VALUE,
      .data = security_instance.sni, .capacity = CONFIG_SIRENA_LWM2M_SERVER_STRING_MAX },
    { .id = 15U, .operations = SIRENA_LWM2M_OP_READ, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_UNSIGNED, .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = 22U, .operations = SIRENA_LWM2M_OP_READ, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_UNSIGNED, .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = 23U, .operations = SIRENA_LWM2M_OP_READ, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_UNSIGNED, .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = 24U, .operations = SIRENA_LWM2M_OP_READ, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_UNSIGNED, .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = 28U, .operations = SIRENA_LWM2M_OP_READ, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_INTEGER, .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = 29U, .operations = SIRENA_LWM2M_OP_READ, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_INTEGER, .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = 30U, .operations = SIRENA_LWM2M_OP_READ, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_STRING, .kind = SIRENA_LWM2M_RESOURCE_VALUE,
      .data = security_instance.rds_application_id,
      .capacity = CONFIG_SIRENA_LWM2M_SERVER_STRING_MAX },
    { .id = 17U, .operations = SIRENA_LWM2M_OP_READ, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_OBJECT_LINK, .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = 26U, .operations = SIRENA_LWM2M_OP_READ, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_OBJECT_LINK, .kind = SIRENA_LWM2M_RESOURCE_VALUE },
#endif
};

static struct sirena_lwm2m_resource_entry server_resources[] = {
    { .id = 0U, .operations = SIRENA_LWM2M_OP_READ, .type = SIRENA_LWM2M_VALUE_INTEGER,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = 1U, .operations = SIRENA_LWM2M_OP_READ | SIRENA_LWM2M_OP_WRITE,
      .type = SIRENA_LWM2M_VALUE_INTEGER, .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = 6U, .operations = SIRENA_LWM2M_OP_READ | SIRENA_LWM2M_OP_WRITE,
      .type = SIRENA_LWM2M_VALUE_BOOLEAN, .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = 7U, .operations = SIRENA_LWM2M_OP_READ | SIRENA_LWM2M_OP_WRITE,
      .type = SIRENA_LWM2M_VALUE_STRING, .kind = SIRENA_LWM2M_RESOURCE_VALUE,
      .data = server_instance.binding, .capacity = CONFIG_SIRENA_LWM2M_SERVER_STRING_MAX },
    { .id = 8U, .operations = SIRENA_LWM2M_OP_EXEC, .type = SIRENA_LWM2M_VALUE_NONE,
      .kind = SIRENA_LWM2M_RESOURCE_EXECUTE },
#if IS_ENABLED(CONFIG_SIRENA_LWM2M_OBJECT_SERVER_OPTIONAL)
    { .id = 2U, .operations = SIRENA_LWM2M_OP_READ | SIRENA_LWM2M_OP_WRITE,
      .kconfig_gated = true, .type = SIRENA_LWM2M_VALUE_INTEGER,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = 3U, .operations = SIRENA_LWM2M_OP_READ | SIRENA_LWM2M_OP_WRITE,
      .kconfig_gated = true, .type = SIRENA_LWM2M_VALUE_INTEGER,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = 4U, .operations = SIRENA_LWM2M_OP_EXEC, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_NONE, .kind = SIRENA_LWM2M_RESOURCE_EXECUTE },
    { .id = 5U, .operations = SIRENA_LWM2M_OP_READ | SIRENA_LWM2M_OP_WRITE,
      .kconfig_gated = true, .type = SIRENA_LWM2M_VALUE_INTEGER,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE },
#endif
};

static struct sirena_lwm2m_resource_entry device_resources[] = {
    { .id = 0U, .operations = SIRENA_LWM2M_OP_READ, .type = SIRENA_LWM2M_VALUE_STRING,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE, .data = device_instance.manufacturer,
      .capacity = CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX },
    { .id = 1U, .operations = SIRENA_LWM2M_OP_READ, .type = SIRENA_LWM2M_VALUE_STRING,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE, .data = device_instance.model_number,
      .capacity = CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX },
    { .id = 2U, .operations = SIRENA_LWM2M_OP_READ, .type = SIRENA_LWM2M_VALUE_STRING,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE, .data = device_instance.serial_number,
      .capacity = CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX },
    { .id = 3U, .operations = SIRENA_LWM2M_OP_READ, .type = SIRENA_LWM2M_VALUE_STRING,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE, .data = device_instance.firmware_version,
      .capacity = CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX },
    { .id = 4U, .operations = SIRENA_LWM2M_OP_EXEC, .type = SIRENA_LWM2M_VALUE_NONE,
      .kind = SIRENA_LWM2M_RESOURCE_EXECUTE },
    { .id = 11U, .operations = SIRENA_LWM2M_OP_READ, .type = SIRENA_LWM2M_VALUE_INTEGER,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = 16U, .operations = SIRENA_LWM2M_OP_READ, .type = SIRENA_LWM2M_VALUE_STRING,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE, .data = device_instance.supported_binding,
      .capacity = CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX },
    { .id = 17U, .operations = SIRENA_LWM2M_OP_READ, .type = SIRENA_LWM2M_VALUE_STRING,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE, .data = device_instance.device_type,
      .capacity = CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX },
    { .id = 18U, .operations = SIRENA_LWM2M_OP_READ, .type = SIRENA_LWM2M_VALUE_STRING,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE, .data = device_instance.hardware_version,
      .capacity = CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX },
    { .id = 19U, .operations = SIRENA_LWM2M_OP_READ, .type = SIRENA_LWM2M_VALUE_STRING,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE, .data = device_instance.software_version,
      .capacity = CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX },
#if IS_ENABLED(CONFIG_SIRENA_LWM2M_OBJECT_DEVICE_OPTIONAL)
    { .id = 5U, .operations = SIRENA_LWM2M_OP_EXEC, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_NONE, .kind = SIRENA_LWM2M_RESOURCE_EXECUTE },
    { .id = 9U, .operations = SIRENA_LWM2M_OP_READ, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_INTEGER, .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = 10U, .operations = SIRENA_LWM2M_OP_READ, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_INTEGER, .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = 12U, .operations = SIRENA_LWM2M_OP_EXEC, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_NONE, .kind = SIRENA_LWM2M_RESOURCE_EXECUTE },
    { .id = 13U, .operations = SIRENA_LWM2M_OP_READ | SIRENA_LWM2M_OP_WRITE,
      .kconfig_gated = true, .type = SIRENA_LWM2M_VALUE_INTEGER,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = 14U, .operations = SIRENA_LWM2M_OP_READ | SIRENA_LWM2M_OP_WRITE,
      .kconfig_gated = true, .type = SIRENA_LWM2M_VALUE_STRING,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE, .data = device_instance.utc_offset,
      .capacity = CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX },
    { .id = 15U, .operations = SIRENA_LWM2M_OP_READ | SIRENA_LWM2M_OP_WRITE,
      .kconfig_gated = true, .type = SIRENA_LWM2M_VALUE_STRING,
      .kind = SIRENA_LWM2M_RESOURCE_VALUE, .data = device_instance.timezone,
      .capacity = CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX },
    { .id = 20U, .operations = SIRENA_LWM2M_OP_READ, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_INTEGER, .kind = SIRENA_LWM2M_RESOURCE_VALUE },
    { .id = 21U, .operations = SIRENA_LWM2M_OP_READ, .kconfig_gated = true,
      .type = SIRENA_LWM2M_VALUE_INTEGER, .kind = SIRENA_LWM2M_RESOURCE_VALUE },
#endif
};

static const struct sirena_lwm2m_object_definition object_definitions[] = {
    {
        .object_id = SIRENA_LWM2M_SECURITY_OBJECT_ID,
        .resources = security_resources,
        .resource_count = ARRAY_SIZE(security_resources),
    },
    {
        .object_id = SIRENA_LWM2M_SERVER_OBJECT_ID,
        .resources = server_resources,
        .resource_count = ARRAY_SIZE(server_resources),
    },
    {
        .object_id = SIRENA_LWM2M_DEVICE_OBJECT_ID,
        .resources = device_resources,
        .resource_count = ARRAY_SIZE(device_resources),
    },
};

static sirena_lwm2m_execute_handler_t execute_handler;
static void *execute_user_data;

static int value_read(uint16_t object_id, uint16_t instance_id, uint16_t resource_id,
                      void *user_data, void *buffer, size_t *length)
{
    struct sirena_lwm2m_resource_entry *entry = user_data;
    size_t capacity = (length != NULL) ? *length : 0U;

    (void)object_id;
    (void)instance_id;
    (void)resource_id;

    switch (entry->type) {
    case SIRENA_LWM2M_VALUE_BOOLEAN:
        if (buffer == NULL || capacity < sizeof(entry->boolean)) {
            return -ENOMEM;
        }
        memcpy(buffer, &entry->boolean, sizeof(entry->boolean));
        if (length != NULL) {
            *length = sizeof(entry->boolean);
        }
        return 0;
    case SIRENA_LWM2M_VALUE_INTEGER:
        if (buffer == NULL || capacity < sizeof(entry->integer)) {
            return -ENOMEM;
        }
        memcpy(buffer, &entry->integer, sizeof(entry->integer));
        if (length != NULL) {
            *length = sizeof(entry->integer);
        }
        return 0;
    case SIRENA_LWM2M_VALUE_UNSIGNED:
        if (buffer == NULL || capacity < sizeof(entry->unsigned_integer)) {
            return -ENOMEM;
        }
        memcpy(buffer, &entry->unsigned_integer, sizeof(entry->unsigned_integer));
        if (length != NULL) {
            *length = sizeof(entry->unsigned_integer);
        }
        return 0;
    case SIRENA_LWM2M_VALUE_STRING:
    case SIRENA_LWM2M_VALUE_OPAQUE:
        if (buffer == NULL) {
            return -EINVAL;
        }
        if (capacity < entry->length) {
            return -ENOMEM;
        }
        if (entry->length > 0U) {
            memcpy(buffer, entry->data, entry->length);
        }
        if (length != NULL) {
            *length = entry->length;
        }
        return 0;
    case SIRENA_LWM2M_VALUE_OBJECT_LINK:
        if (buffer == NULL || capacity < 4U) {
            return -ENOMEM;
        }
        ((uint8_t *)buffer)[0] = (uint8_t)(entry->object_link_object_id >> 8);
        ((uint8_t *)buffer)[1] = (uint8_t)(entry->object_link_object_id & 0xFF);
        ((uint8_t *)buffer)[2] = (uint8_t)(entry->object_link_instance_id >> 8);
        ((uint8_t *)buffer)[3] = (uint8_t)(entry->object_link_instance_id & 0xFF);
        if (length != NULL) {
            *length = 4U;
        }
        return 0;
    default:
        return -ENOTSUP;
    }
}

static int value_write(uint16_t object_id, uint16_t instance_id, uint16_t resource_id,
                       void *user_data, const void *buffer, size_t length)
{
    struct sirena_lwm2m_resource_entry *entry = user_data;

    (void)object_id;
    (void)instance_id;
    (void)resource_id;

    switch (entry->type) {
    case SIRENA_LWM2M_VALUE_BOOLEAN:
        if (buffer == NULL || length != sizeof(entry->boolean)) {
            return -EINVAL;
        }
        memcpy(&entry->boolean, buffer, sizeof(entry->boolean));
        return 0;
    case SIRENA_LWM2M_VALUE_INTEGER:
        if (buffer == NULL) {
            return -EINVAL;
        }
        if (length == sizeof(int32_t)) {
            int32_t narrow;

            memcpy(&narrow, buffer, sizeof(narrow));
            entry->integer = narrow;
        } else if (length == sizeof(int64_t)) {
            memcpy(&entry->integer, buffer, sizeof(entry->integer));
        } else {
            return -EINVAL;
        }
        return 0;
    case SIRENA_LWM2M_VALUE_UNSIGNED:
        if (buffer == NULL) {
            return -EINVAL;
        }
        if (length == sizeof(uint32_t)) {
            uint32_t narrow;

            memcpy(&narrow, buffer, sizeof(narrow));
            entry->unsigned_integer = narrow;
        } else if (length == sizeof(uint64_t)) {
            memcpy(&entry->unsigned_integer, buffer, sizeof(entry->unsigned_integer));
        } else {
            return -EINVAL;
        }
        return 0;
    case SIRENA_LWM2M_VALUE_STRING:
        if (buffer == NULL) {
            return -EINVAL;
        }
        if (length > entry->capacity) {
            return -ENOMEM;
        }
        memcpy(entry->data, buffer, length);
        entry->data[length] = '\0';
        entry->length = (uint16_t)length;
        return 0;
    case SIRENA_LWM2M_VALUE_OPAQUE:
        if (length > entry->capacity) {
            return -ENOMEM;
        }
        memset(entry->data, 0, entry->capacity);
        if (length > 0U) {
            if (buffer == NULL) {
                return -EINVAL;
            }
            memcpy(entry->data, buffer, length);
        }
        entry->length = (uint16_t)length;
        return 0;
    case SIRENA_LWM2M_VALUE_OBJECT_LINK:
        if (buffer == NULL || length != 4U) {
            return -EINVAL;
        }
        entry->object_link_object_id = ((uint16_t)((const uint8_t *)buffer)[0] << 8) |
                                      ((const uint8_t *)buffer)[1];
        entry->object_link_instance_id = ((uint16_t)((const uint8_t *)buffer)[2] << 8) |
                                         ((const uint8_t *)buffer)[3];
        return 0;
    default:
        return -ENOTSUP;
    }
}

static int builtin_execute(uint16_t object_id, uint16_t instance_id, uint16_t resource_id,
                           void *user_data, const void *args, size_t args_length)
{
    (void)user_data;
    (void)args;
    (void)args_length;

    if (execute_handler == NULL) {
        return -ENOTSUP;
    }

    execute_handler(object_id, instance_id, resource_id, execute_user_data);

    return 0;
}

static int resources_add(uint16_t object_id, uint16_t instance_id,
                         struct sirena_lwm2m_resource_entry *entries, size_t count)
{
    size_t i;
    int rc;

    for (i = 0U; i < count; ++i) {
        struct sirena_lwm2m_resource_entry *entry = &entries[i];

        if (entry->kind == SIRENA_LWM2M_RESOURCE_EXECUTE) {
            rc = sirena_lwm2m_add_resource(object_id, instance_id, entry->id,
                                           entry->type, entry->operations,
                                           entry->kconfig_gated, NULL, NULL,
                                           builtin_execute, entry);
        } else if (entry->kind == SIRENA_LWM2M_RESOURCE_CONSTANT) {
            rc = sirena_lwm2m_add_resource(object_id, instance_id, entry->id,
                                           entry->type, entry->operations,
                                           entry->kconfig_gated, value_read,
                                           NULL, NULL, entry);
        } else {
            rc = sirena_lwm2m_add_resource(object_id, instance_id, entry->id,
                                           entry->type, entry->operations,
                                           entry->kconfig_gated, value_read,
                                           value_write, NULL, entry);
        }

        if (rc == 0) {
            entry->length = 0U;
            entry->integer = 0;
            entry->unsigned_integer = 0U;
            entry->boolean = false;
        } else if (rc != -EEXIST) {
            return rc;
        }
    }

    return 0;
}

int sirena_lwm2m_objects_init(void)
{
    size_t i;

    for (i = 0U; i < ARRAY_SIZE(object_definitions); ++i) {
        const struct sirena_lwm2m_object_definition *definition = &object_definitions[i];
        int rc;

        rc = sirena_lwm2m_register_object(definition->object_id, false);
        if (rc != 0) {
            return rc;
        }

        rc = sirena_lwm2m_create_instance(definition->object_id, 0U, NULL);
        if (rc != 0 && rc != -EEXIST) {
            return rc;
        }

        rc = resources_add(definition->object_id, 0U, definition->resources,
                           definition->resource_count);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}

static const struct sirena_lwm2m_object_definition *find_object_definition(uint16_t object_id)
{
    size_t i;

    for (i = 0U; i < ARRAY_SIZE(object_definitions); ++i) {
        if (object_definitions[i].object_id == object_id) {
            return &object_definitions[i];
        }
    }

    return NULL;
}

static int create_object_instance(uint16_t object_id, uint16_t instance_id)
{
    const struct sirena_lwm2m_object_definition *definition;
    int rc;

    definition = find_object_definition(object_id);
    if (definition == NULL) {
        return -ENOENT;
    }

    rc = sirena_lwm2m_register_object(definition->object_id, false);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_create_instance(definition->object_id, instance_id, NULL);
    if (rc != 0) {
        return rc;
    }

    rc = resources_add(definition->object_id, instance_id, definition->resources,
                      definition->resource_count);
    if (rc != 0) {
        sirena_lwm2m_delete_instance(definition->object_id, instance_id);
        return rc;
    }

    return 0;
}

static int delete_object_instance(uint16_t object_id, uint16_t instance_id)
{
    return sirena_lwm2m_delete_instance(object_id, instance_id);
}

int sirena_lwm2m_create_object_instance(uint16_t object_id, uint16_t instance_id)
{
    return create_object_instance(object_id, instance_id);
}

int sirena_lwm2m_object_create_instance(uint16_t object_id, uint16_t instance_id)
{
    return create_object_instance(object_id, instance_id);
}

int sirena_lwm2m_objects_create_instance(uint16_t object_id, uint16_t instance_id)
{
    return create_object_instance(object_id, instance_id);
}

int sirena_lwm2m_delete_object_instance(uint16_t object_id, uint16_t instance_id)
{
    return delete_object_instance(object_id, instance_id);
}

int sirena_lwm2m_object_delete_instance(uint16_t object_id, uint16_t instance_id)
{
    return delete_object_instance(object_id, instance_id);
}

int sirena_lwm2m_objects_delete_instance(uint16_t object_id, uint16_t instance_id)
{
    return delete_object_instance(object_id, instance_id);
}

static int resource_write(uint16_t object_id, uint16_t instance_id, uint16_t resource_id,
                          enum sirena_lwm2m_value_type type, const void *buffer,
                          size_t length)
{
    struct sirena_lwm2m_resource *resource;
    int rc;

    rc = sirena_lwm2m_find_resource(object_id, instance_id, resource_id, &resource);
    if (rc != 0) {
        return rc;
    }

    if (resource->write == NULL) {
        return -ENOTSUP;
    }

    if (resource->type != type) {
        return -EINVAL;
    }

    return resource->write(object_id, instance_id, resource_id, resource->user_data,
                           buffer, length);
}

static int resource_read(uint16_t object_id, uint16_t instance_id, uint16_t resource_id,
                         enum sirena_lwm2m_value_type type, void *buffer, size_t *length)
{
    struct sirena_lwm2m_resource *resource;
    int rc;

    rc = sirena_lwm2m_find_resource(object_id, instance_id, resource_id, &resource);
    if (rc != 0) {
        return rc;
    }

    if (resource->read == NULL) {
        return -ENOTSUP;
    }

    if (resource->type != type) {
        return -EINVAL;
    }

    return resource->read(object_id, instance_id, resource_id, resource->user_data,
                          buffer, length);
}

static int value_write_typed(uint16_t object_id, uint16_t instance_id,
                             uint16_t resource_id, const struct sirena_lwm2m_value *value)
{
    switch (value->type) {
    case SIRENA_LWM2M_VALUE_BOOLEAN:
        return resource_write(object_id, instance_id, resource_id,
                              SIRENA_LWM2M_VALUE_BOOLEAN, &value->data.boolean,
                              sizeof(value->data.boolean));
    case SIRENA_LWM2M_VALUE_INTEGER:
        return resource_write(object_id, instance_id, resource_id,
                              SIRENA_LWM2M_VALUE_INTEGER, &value->data.integer,
                              sizeof(value->data.integer));
    case SIRENA_LWM2M_VALUE_UNSIGNED:
        return resource_write(object_id, instance_id, resource_id,
                              SIRENA_LWM2M_VALUE_UNSIGNED, &value->data.unsigned_integer,
                              sizeof(value->data.unsigned_integer));
    case SIRENA_LWM2M_VALUE_STRING:
        if (value->data.string.value == NULL) {
            return -EINVAL;
        }
        return resource_write(object_id, instance_id, resource_id,
                              SIRENA_LWM2M_VALUE_STRING, value->data.string.value,
                              value->data.string.length);
    case SIRENA_LWM2M_VALUE_OPAQUE:
        if (value->data.opaque.value == NULL && value->data.opaque.length > 0U) {
            return -EINVAL;
        }
        return resource_write(object_id, instance_id, resource_id,
                              SIRENA_LWM2M_VALUE_OPAQUE, value->data.opaque.value,
                              value->data.opaque.length);
    case SIRENA_LWM2M_VALUE_OBJECT_LINK: {
        uint8_t buf[4];
        buf[0] = (uint8_t)(value->data.object_link.object_id >> 8);
        buf[1] = (uint8_t)(value->data.object_link.object_id & 0xFF);
        buf[2] = (uint8_t)(value->data.object_link.instance_id >> 8);
        buf[3] = (uint8_t)(value->data.object_link.instance_id & 0xFF);
        return resource_write(object_id, instance_id, resource_id,
                              SIRENA_LWM2M_VALUE_OBJECT_LINK, buf, 4U);
    }
    default:
        return -ENOTSUP;
    }
}

int sirena_lwm2m_set_value(uint16_t object_id, uint16_t instance_id,
                           uint16_t resource_id, const struct sirena_lwm2m_value *value)
{
    struct sirena_lwm2m_set_value_request request;
    k_tid_t engine_tid;

    if (value == NULL) {
        return -EINVAL;
    }

    engine_tid = sirena_lwm2m_engine_thread_id();
    if (engine_tid == NULL || engine_tid == k_current_get()) {
        return value_write_typed(object_id, instance_id, resource_id, value);
    }

    request.object_id = object_id;
    request.instance_id = instance_id;
    request.resource_id = resource_id;
    request.value = value;

    return sirena_lwm2m_engine_submit(SIRENA_LWM2M_ENGINE_OPERATION_SET_VALUE, &request);
}

int sirena_lwm2m_set_string(uint16_t object_id, uint16_t instance_id,
                            uint16_t resource_id, const char *value)
{
    if (value == NULL) {
        return -EINVAL;
    }

    return resource_write(object_id, instance_id, resource_id, SIRENA_LWM2M_VALUE_STRING,
                          value, strlen(value));
}

int sirena_lwm2m_get_string(uint16_t object_id, uint16_t instance_id,
                            uint16_t resource_id, char *buffer, size_t *length)
{
    size_t capacity;
    size_t produced;
    int rc;

    if (buffer == NULL || length == NULL) {
        return -EINVAL;
    }

    capacity = *length;
    if (capacity == 0U) {
        return -ENOMEM;
    }

    produced = capacity - 1U;

    rc = resource_read(object_id, instance_id, resource_id, SIRENA_LWM2M_VALUE_STRING,
                       buffer, &produced);
    if (rc != 0) {
        return rc;
    }

    buffer[produced] = '\0';
    *length = produced;

    return 0;
}

int sirena_lwm2m_set_integer(uint16_t object_id, uint16_t instance_id,
                             uint16_t resource_id, int64_t value)
{
    return resource_write(object_id, instance_id, resource_id, SIRENA_LWM2M_VALUE_INTEGER,
                          &value, sizeof(value));
}

int sirena_lwm2m_get_integer(uint16_t object_id, uint16_t instance_id,
                             uint16_t resource_id, int64_t *value)
{
    size_t length = sizeof(*value);
    int rc;

    if (value == NULL) {
        return -EINVAL;
    }

    rc = resource_read(object_id, instance_id, resource_id, SIRENA_LWM2M_VALUE_INTEGER,
                       value, &length);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int sirena_lwm2m_set_boolean(uint16_t object_id, uint16_t instance_id,
                             uint16_t resource_id, bool value)
{
    return resource_write(object_id, instance_id, resource_id, SIRENA_LWM2M_VALUE_BOOLEAN,
                          &value, sizeof(value));
}

int sirena_lwm2m_get_boolean(uint16_t object_id, uint16_t instance_id,
                             uint16_t resource_id, bool *value)
{
    size_t length = sizeof(*value);
    int rc;

    if (value == NULL) {
        return -EINVAL;
    }

    rc = resource_read(object_id, instance_id, resource_id, SIRENA_LWM2M_VALUE_BOOLEAN,
                       value, &length);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

int sirena_lwm2m_set_opaque(uint16_t object_id, uint16_t instance_id,
                            uint16_t resource_id, const void *data, size_t length)
{
    if (data == NULL && length > 0U) {
        return -EINVAL;
    }

    return resource_write(object_id, instance_id, resource_id, SIRENA_LWM2M_VALUE_OPAQUE,
                          data, length);
}

int sirena_lwm2m_get_opaque(uint16_t object_id, uint16_t instance_id,
                            uint16_t resource_id, void *buffer, size_t *length)
{
    if (buffer == NULL || length == NULL) {
        return -EINVAL;
    }

    return resource_read(object_id, instance_id, resource_id, SIRENA_LWM2M_VALUE_OPAQUE,
                         buffer, length);
}

int sirena_lwm2m_get_opaque_ref(uint16_t object_id, uint16_t instance_id,
                                uint16_t resource_id, const void **data, size_t *length)
{
    struct sirena_lwm2m_resource *resource;
    struct sirena_lwm2m_resource_entry *entry;
    int rc;

    if (data == NULL || length == NULL) {
        return -EINVAL;
    }

    rc = sirena_lwm2m_find_resource(object_id, instance_id, resource_id, &resource);
    if (rc != 0) {
        return rc;
    }

    if (resource->type != SIRENA_LWM2M_VALUE_OPAQUE) {
        return -EINVAL;
    }

    entry = resource->user_data;
    if (entry == NULL || entry->data == NULL) {
        return -ENOENT;
    }

    *data = entry->data;
    *length = entry->length;

    return 0;
}

int sirena_lwm2m_set_object_link(uint16_t object_id, uint16_t instance_id,
                            uint16_t resource_id, uint16_t linked_object_id,
                            uint16_t linked_instance_id)
{
    uint8_t buffer[4];

    buffer[0] = (uint8_t)(linked_object_id >> 8);
    buffer[1] = (uint8_t)(linked_object_id & 0xFF);
    buffer[2] = (uint8_t)(linked_instance_id >> 8);
    buffer[3] = (uint8_t)(linked_instance_id & 0xFF);

    return resource_write(object_id, instance_id, resource_id,
                          SIRENA_LWM2M_VALUE_OBJECT_LINK, buffer, 4U);
}

int sirena_lwm2m_get_object_link(uint16_t object_id, uint16_t instance_id,
                            uint16_t resource_id, uint16_t *linked_object_id,
                            uint16_t *linked_instance_id)
{
    uint8_t buffer[4];
    size_t length = sizeof(buffer);
    int rc;

    if (linked_object_id == NULL || linked_instance_id == NULL) {
        return -EINVAL;
    }

    rc = resource_read(object_id, instance_id, resource_id,
                       SIRENA_LWM2M_VALUE_OBJECT_LINK, buffer, &length);
    if (rc != 0) {
        return rc;
    }

    if (length != 4U) {
        return -EINVAL;
    }

    *linked_object_id = ((uint16_t)buffer[0] << 8) | buffer[1];
    *linked_instance_id = ((uint16_t)buffer[2] << 8) | buffer[3];

    return 0;
}

int sirena_lwm2m_set_execute_handler(sirena_lwm2m_execute_handler_t handler,
                                     void *user_data)
{
    execute_handler = handler;
    execute_user_data = user_data;

    return 0;
}

static int objects_ensure(void)
{
    struct sirena_lwm2m_instance *instance;

    if (sirena_lwm2m_find_instance(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U, &instance) == 0 &&
        sirena_lwm2m_find_instance(SIRENA_LWM2M_SERVER_OBJECT_ID, 0U, &instance) == 0) {
        return 0;
    }

    return sirena_lwm2m_objects_init();
}

int sirena_lwm2m_provision(const struct sirena_lwm2m_provisioning *provisioning)
{
    int rc;

    if (provisioning == NULL) {
        return -EINVAL;
    }

    if (provisioning->security_mode > SIRENA_LWM2M_SECURITY_MODE_NOSEC) {
        return -EINVAL;
    }

    rc = objects_ensure();
    if (rc != 0) {
        return rc;
    }

    if (provisioning->server_uri != NULL) {
        rc = sirena_lwm2m_set_string(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                 SIRENA_LWM2M_SECURITY_URI_RESOURCE,
                                     provisioning->server_uri);
        if (rc != 0) {
            return rc;
        }
    }

    rc = sirena_lwm2m_set_integer(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                  SIRENA_LWM2M_SECURITY_MODE_RESOURCE,
                                  (int64_t)provisioning->security_mode);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_set_opaque(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                 SIRENA_LWM2M_SECURITY_IDENTITY_RESOURCE,
                                 provisioning->identity, provisioning->identity_length);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_set_opaque(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                 SIRENA_LWM2M_SECURITY_SERVER_PUBLIC_KEY_RESOURCE,
                                 provisioning->server_public_key,
                                 provisioning->server_public_key_length);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_set_opaque(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                 SIRENA_LWM2M_SECURITY_SECRET_KEY_RESOURCE,
                                 provisioning->secret_key, provisioning->secret_key_length);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_set_integer(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                  SIRENA_LWM2M_SECURITY_SHORT_SERVER_ID_RESOURCE,
                                  (int64_t)provisioning->short_server_id);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_set_integer(SIRENA_LWM2M_SERVER_OBJECT_ID, 0U, 0U,
                                  (int64_t)provisioning->short_server_id);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_set_integer(SIRENA_LWM2M_SERVER_OBJECT_ID, 0U, 1U,
                                  (int64_t)provisioning->lifetime);
    if (rc != 0) {
        return rc;
    }

    if (provisioning->binding != NULL) {
        rc = sirena_lwm2m_set_string(SIRENA_LWM2M_SERVER_OBJECT_ID, 0U, 7U,
                                     provisioning->binding);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}
