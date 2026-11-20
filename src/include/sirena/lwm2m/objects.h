#ifndef SIRENA_LWM2M_OBJECTS_H
#define SIRENA_LWM2M_OBJECTS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

enum sirena_lwm2m_security_mode {
    SIRENA_LWM2M_SECURITY_MODE_PSK = 0,
    SIRENA_LWM2M_SECURITY_MODE_RPK = 1,
    SIRENA_LWM2M_SECURITY_MODE_CERTIFICATE = 2,
    SIRENA_LWM2M_SECURITY_MODE_NOSEC = 3,
};

struct sirena_lwm2m_provisioning {
    const char *server_uri;
    uint8_t security_mode;
    const void *identity;
    size_t identity_length;
    const void *server_public_key;
    size_t server_public_key_length;
    const void *secret_key;
    size_t secret_key_length;
    uint16_t short_server_id;
    uint32_t lifetime;
    const char *binding;
};

typedef void (*sirena_lwm2m_execute_handler_t)(uint16_t object_id,
                                               uint16_t instance_id,
                                               uint16_t resource_id,
                                               void *user_data);

int sirena_lwm2m_objects_init(void);

int sirena_lwm2m_set_string(uint16_t object_id, uint16_t instance_id,
                            uint16_t resource_id, const char *value);
int sirena_lwm2m_get_string(uint16_t object_id, uint16_t instance_id,
                            uint16_t resource_id, char *buffer, size_t *length);
int sirena_lwm2m_set_integer(uint16_t object_id, uint16_t instance_id,
                             uint16_t resource_id, int64_t value);
int sirena_lwm2m_get_integer(uint16_t object_id, uint16_t instance_id,
                             uint16_t resource_id, int64_t *value);
int sirena_lwm2m_set_boolean(uint16_t object_id, uint16_t instance_id,
                             uint16_t resource_id, bool value);
int sirena_lwm2m_get_boolean(uint16_t object_id, uint16_t instance_id,
                             uint16_t resource_id, bool *value);
int sirena_lwm2m_set_opaque(uint16_t object_id, uint16_t instance_id,
                            uint16_t resource_id, const void *data, size_t length);
int sirena_lwm2m_get_opaque(uint16_t object_id, uint16_t instance_id,
                            uint16_t resource_id, void *buffer, size_t *length);

int sirena_lwm2m_set_execute_handler(sirena_lwm2m_execute_handler_t handler,
                                     void *user_data);

int sirena_lwm2m_provision(const struct sirena_lwm2m_provisioning *provisioning);

#endif
