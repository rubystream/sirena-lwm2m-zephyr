#include <sirena/lwm2m/objects.h>
#include <sirena/lwm2m/object_registry.h>
#include <zephyr/sys/util.h>
#include <string.h>
#include <errno.h>

#define SIRENA_LWM2M_SECURITY_OBJECT_ID 0U
#define SIRENA_LWM2M_SERVER_OBJECT_ID 1U
#define SIRENA_LWM2M_DEVICE_OBJECT_ID 3U

enum sirena_lwm2m_resource_kind {
    SIRENA_LWM2M_RESOURCE_VALUE,
    SIRENA_LWM2M_RESOURCE_CONSTANT,
    SIRENA_LWM2M_RESOURCE_EXECUTE
};

struct sirena_lwm2m_resource_entry {
    uint16_t id;
    uint8_t operations;
    bool optional;
    enum sirena_lwm2m_value_type type;
    enum sirena_lwm2m_resource_kind kind;
    uint8_t *data;
    uint16_t capacity;
    uint16_t length;
    int64_t integer;
    uint64_t unsigned_integer;
    bool boolean;
};

struct sirena_lwm2m_security_instance {
    char server_uri[CONFIG_SIRENA_LWM2M_SERVER_URI_MAX + 1U];
    uint8_t identity[CONFIG_SIRENA_LWM2M_CREDENTIAL_MAX];
    uint8_t server_public_key[CONFIG_SIRENA_LWM2M_CREDENTIAL_MAX];
    uint8_t secret_key[CONFIG_SIRENA_LWM2M_CREDENTIAL_MAX];
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
    { 0U, SIRENA_LWM2M_OP_READ, false, SIRENA_LWM2M_VALUE_STRING,
      SIRENA_LWM2M_RESOURCE_VALUE, security_instance.server_uri,
      CONFIG_SIRENA_LWM2M_SERVER_URI_MAX, 0U, 0, 0U, false },
    { 1U, SIRENA_LWM2M_OP_READ, false, SIRENA_LWM2M_VALUE_BOOLEAN,
      SIRENA_LWM2M_RESOURCE_CONSTANT, NULL, 0U, 0U, 0, 0U, false },
    { 2U, SIRENA_LWM2M_OP_READ, false, SIRENA_LWM2M_VALUE_INTEGER,
      SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 3U, SIRENA_LWM2M_OP_READ, false, SIRENA_LWM2M_VALUE_OPAQUE,
      SIRENA_LWM2M_RESOURCE_VALUE, security_instance.identity,
      CONFIG_SIRENA_LWM2M_CREDENTIAL_MAX, 0U, 0, 0U, false },
    { 4U, SIRENA_LWM2M_OP_READ, false, SIRENA_LWM2M_VALUE_OPAQUE,
      SIRENA_LWM2M_RESOURCE_VALUE, security_instance.server_public_key,
      CONFIG_SIRENA_LWM2M_CREDENTIAL_MAX, 0U, 0, 0U, false },
    { 5U, SIRENA_LWM2M_OP_READ, false, SIRENA_LWM2M_VALUE_OPAQUE,
      SIRENA_LWM2M_RESOURCE_VALUE, security_instance.secret_key,
      CONFIG_SIRENA_LWM2M_CREDENTIAL_MAX, 0U, 0, 0U, false },
    { 10U, SIRENA_LWM2M_OP_READ, false, SIRENA_LWM2M_VALUE_INTEGER,
      SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
#if IS_ENABLED(CONFIG_SIRENA_LWM2M_OBJECT_SECURITY_OPTIONAL)
    { 6U, SIRENA_LWM2M_OP_READ, true, SIRENA_LWM2M_VALUE_INTEGER,
      SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 7U, SIRENA_LWM2M_OP_READ, true, SIRENA_LWM2M_VALUE_OPAQUE,
      SIRENA_LWM2M_RESOURCE_VALUE, security_instance.sms_binding_key_parameters,
      CONFIG_SIRENA_LWM2M_CREDENTIAL_MAX, 0U, 0, 0U, false },
    { 8U, SIRENA_LWM2M_OP_READ, true, SIRENA_LWM2M_VALUE_OPAQUE,
      SIRENA_LWM2M_RESOURCE_VALUE, security_instance.sms_binding_secret_key,
      CONFIG_SIRENA_LWM2M_CREDENTIAL_MAX, 0U, 0, 0U, false },
    { 9U, SIRENA_LWM2M_OP_READ, true, SIRENA_LWM2M_VALUE_STRING,
      SIRENA_LWM2M_RESOURCE_VALUE, security_instance.server_sms_number,
      CONFIG_SIRENA_LWM2M_SERVER_STRING_MAX, 0U, 0, 0U, false },
    { 11U, SIRENA_LWM2M_OP_READ, true, SIRENA_LWM2M_VALUE_INTEGER,
      SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 12U, SIRENA_LWM2M_OP_READ, true, SIRENA_LWM2M_VALUE_INTEGER,
      SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 13U, SIRENA_LWM2M_OP_READ, true, SIRENA_LWM2M_VALUE_UNSIGNED,
      SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 14U, SIRENA_LWM2M_OP_READ, true, SIRENA_LWM2M_VALUE_STRING,
      SIRENA_LWM2M_RESOURCE_VALUE, security_instance.sni,
      CONFIG_SIRENA_LWM2M_SERVER_STRING_MAX, 0U, 0, 0U, false },
    { 15U, SIRENA_LWM2M_OP_READ, true, SIRENA_LWM2M_VALUE_UNSIGNED,
      SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 17U, SIRENA_LWM2M_OP_READ, true, SIRENA_LWM2M_VALUE_INTEGER,
      SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 22U, SIRENA_LWM2M_OP_READ, true, SIRENA_LWM2M_VALUE_UNSIGNED,
      SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 23U, SIRENA_LWM2M_OP_READ, true, SIRENA_LWM2M_VALUE_UNSIGNED,
      SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 24U, SIRENA_LWM2M_OP_READ, true, SIRENA_LWM2M_VALUE_UNSIGNED,
      SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 26U, SIRENA_LWM2M_OP_READ, true, SIRENA_LWM2M_VALUE_INTEGER,
      SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 28U, SIRENA_LWM2M_OP_READ, true, SIRENA_LWM2M_VALUE_INTEGER,
      SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 29U, SIRENA_LWM2M_OP_READ, true, SIRENA_LWM2M_VALUE_INTEGER,
      SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 30U, SIRENA_LWM2M_OP_READ, true, SIRENA_LWM2M_VALUE_STRING,
      SIRENA_LWM2M_RESOURCE_VALUE, security_instance.rds_application_id,
      CONFIG_SIRENA_LWM2M_SERVER_STRING_MAX, 0U, 0, 0U, false },
#endif
};

static struct sirena_lwm2m_resource_entry server_resources[] = {
    { 0U, SIRENA_LWM2M_OP_READ, false, SIRENA_LWM2M_VALUE_INTEGER,
      SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 1U, SIRENA_LWM2M_OP_READ | SIRENA_LWM2M_OP_WRITE, false,
      SIRENA_LWM2M_VALUE_INTEGER, SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 6U, SIRENA_LWM2M_OP_READ | SIRENA_LWM2M_OP_WRITE, false,
      SIRENA_LWM2M_VALUE_BOOLEAN, SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 7U, SIRENA_LWM2M_OP_READ | SIRENA_LWM2M_OP_WRITE, false,
      SIRENA_LWM2M_VALUE_STRING, SIRENA_LWM2M_RESOURCE_VALUE, server_instance.binding,
      CONFIG_SIRENA_LWM2M_SERVER_STRING_MAX, 0U, 0, 0U, false },
    { 8U, SIRENA_LWM2M_OP_EXEC, false, SIRENA_LWM2M_VALUE_NONE,
      SIRENA_LWM2M_RESOURCE_EXECUTE, NULL, 0U, 0U, 0, 0U, false },
#if IS_ENABLED(CONFIG_SIRENA_LWM2M_OBJECT_SERVER_OPTIONAL)
    { 2U, SIRENA_LWM2M_OP_READ | SIRENA_LWM2M_OP_WRITE, true,
      SIRENA_LWM2M_VALUE_INTEGER, SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 3U, SIRENA_LWM2M_OP_READ | SIRENA_LWM2M_OP_WRITE, true,
      SIRENA_LWM2M_VALUE_INTEGER, SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 4U, SIRENA_LWM2M_OP_EXEC, true, SIRENA_LWM2M_VALUE_NONE,
      SIRENA_LWM2M_RESOURCE_EXECUTE, NULL, 0U, 0U, 0, 0U, false },
    { 5U, SIRENA_LWM2M_OP_READ | SIRENA_LWM2M_OP_WRITE, true,
      SIRENA_LWM2M_VALUE_INTEGER, SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
#endif
};

static struct sirena_lwm2m_resource_entry device_resources[] = {
    { 0U, SIRENA_LWM2M_OP_READ, false, SIRENA_LWM2M_VALUE_STRING,
      SIRENA_LWM2M_RESOURCE_VALUE, device_instance.manufacturer,
      CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX, 0U, 0, 0U, false },
    { 1U, SIRENA_LWM2M_OP_READ, false, SIRENA_LWM2M_VALUE_STRING,
      SIRENA_LWM2M_RESOURCE_VALUE, device_instance.model_number,
      CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX, 0U, 0, 0U, false },
    { 2U, SIRENA_LWM2M_OP_READ, false, SIRENA_LWM2M_VALUE_STRING,
      SIRENA_LWM2M_RESOURCE_VALUE, device_instance.serial_number,
      CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX, 0U, 0, 0U, false },
    { 3U, SIRENA_LWM2M_OP_READ, false, SIRENA_LWM2M_VALUE_STRING,
      SIRENA_LWM2M_RESOURCE_VALUE, device_instance.firmware_version,
      CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX, 0U, 0, 0U, false },
    { 4U, SIRENA_LWM2M_OP_EXEC, false, SIRENA_LWM2M_VALUE_NONE,
      SIRENA_LWM2M_RESOURCE_EXECUTE, NULL, 0U, 0U, 0, 0U, false },
    { 11U, SIRENA_LWM2M_OP_READ, false, SIRENA_LWM2M_VALUE_INTEGER,
      SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 16U, SIRENA_LWM2M_OP_READ, false, SIRENA_LWM2M_VALUE_STRING,
      SIRENA_LWM2M_RESOURCE_VALUE, device_instance.supported_binding,
      CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX, 0U, 0, 0U, false },
    { 17U, SIRENA_LWM2M_OP_READ, false, SIRENA_LWM2M_VALUE_STRING,
      SIRENA_LWM2M_RESOURCE_VALUE, device_instance.device_type,
      CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX, 0U, 0, 0U, false },
    { 18U, SIRENA_LWM2M_OP_READ, false, SIRENA_LWM2M_VALUE_STRING,
      SIRENA_LWM2M_RESOURCE_VALUE, device_instance.hardware_version,
      CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX, 0U, 0, 0U, false },
    { 19U, SIRENA_LWM2M_OP_READ, false, SIRENA_LWM2M_VALUE_STRING,
      SIRENA_LWM2M_RESOURCE_VALUE, device_instance.software_version,
      CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX, 0U, 0, 0U, false },
#if IS_ENABLED(CONFIG_SIRENA_LWM2M_OBJECT_DEVICE_OPTIONAL)
    { 5U, SIRENA_LWM2M_OP_EXEC, true, SIRENA_LWM2M_VALUE_NONE,
      SIRENA_LWM2M_RESOURCE_EXECUTE, NULL, 0U, 0U, 0, 0U, false },
    { 9U, SIRENA_LWM2M_OP_READ, true, SIRENA_LWM2M_VALUE_INTEGER,
      SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 10U, SIRENA_LWM2M_OP_READ, true, SIRENA_LWM2M_VALUE_INTEGER,
      SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 12U, SIRENA_LWM2M_OP_EXEC, true, SIRENA_LWM2M_VALUE_NONE,
      SIRENA_LWM2M_RESOURCE_EXECUTE, NULL, 0U, 0U, 0, 0U, false },
    { 13U, SIRENA_LWM2M_OP_READ | SIRENA_LWM2M_OP_WRITE, true,
      SIRENA_LWM2M_VALUE_INTEGER, SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 14U, SIRENA_LWM2M_OP_READ | SIRENA_LWM2M_OP_WRITE, true,
      SIRENA_LWM2M_VALUE_STRING, SIRENA_LWM2M_RESOURCE_VALUE, device_instance.utc_offset,
      CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX, 0U, 0, 0U, false },
    { 15U, SIRENA_LWM2M_OP_READ | SIRENA_LWM2M_OP_WRITE, true,
      SIRENA_LWM2M_VALUE_STRING, SIRENA_LWM2M_RESOURCE_VALUE, device_instance.timezone,
      CONFIG_SIRENA_LWM2M_DEVICE_STRING_MAX, 0U, 0, 0U, false },
    { 20U, SIRENA_LWM2M_OP_READ, true, SIRENA_LWM2M_VALUE_INTEGER,
      SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
    { 21U, SIRENA_LWM2M_OP_READ, true, SIRENA_LWM2M_VALUE_INTEGER,
      SIRENA_LWM2M_RESOURCE_VALUE, NULL, 0U, 0U, 0, 0U, false },
#endif
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
        if (length > 0U) {
            if (buffer == NULL) {
                return -EINVAL;
            }
            memcpy(entry->data, buffer, length);
        }
        entry->length = (uint16_t)length;
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
                                           entry->optional, NULL, NULL,
                                           builtin_execute, entry);
        } else if (entry->kind == SIRENA_LWM2M_RESOURCE_CONSTANT) {
            rc = sirena_lwm2m_add_resource(object_id, instance_id, entry->id,
                                           entry->type, entry->operations,
                                           entry->optional, value_read,
                                           NULL, NULL, entry);
        } else {
            rc = sirena_lwm2m_add_resource(object_id, instance_id, entry->id,
                                           entry->type, entry->operations,
                                           entry->optional, value_read,
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
    int rc;

    rc = sirena_lwm2m_register_object(SIRENA_LWM2M_SECURITY_OBJECT_ID, false);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_create_instance(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U, NULL);
    if (rc != 0 && rc != -EEXIST) {
        return rc;
    }

    rc = resources_add(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U, security_resources,
                       sizeof(security_resources) / sizeof(security_resources[0]));
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_register_object(SIRENA_LWM2M_SERVER_OBJECT_ID, false);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_create_instance(SIRENA_LWM2M_SERVER_OBJECT_ID, 0U, NULL);
    if (rc != 0 && rc != -EEXIST) {
        return rc;
    }

    rc = resources_add(SIRENA_LWM2M_SERVER_OBJECT_ID, 0U, server_resources,
                       sizeof(server_resources) / sizeof(server_resources[0]));
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_register_object(SIRENA_LWM2M_DEVICE_OBJECT_ID, false);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_create_instance(SIRENA_LWM2M_DEVICE_OBJECT_ID, 0U, NULL);
    if (rc != 0 && rc != -EEXIST) {
        return rc;
    }

    return resources_add(SIRENA_LWM2M_DEVICE_OBJECT_ID, 0U, device_resources,
                         sizeof(device_resources) / sizeof(device_resources[0]));
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
        rc = sirena_lwm2m_set_string(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U, 0U,
                                     provisioning->server_uri);
        if (rc != 0) {
            return rc;
        }
    }

    rc = sirena_lwm2m_set_integer(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U, 2U,
                                  (int64_t)provisioning->security_mode);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_set_opaque(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U, 3U,
                                 provisioning->identity, provisioning->identity_length);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_set_opaque(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U, 4U,
                                 provisioning->server_public_key,
                                 provisioning->server_public_key_length);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_set_opaque(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U, 5U,
                                 provisioning->secret_key, provisioning->secret_key_length);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_set_integer(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U, 10U,
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
