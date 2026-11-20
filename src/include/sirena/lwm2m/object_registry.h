#ifndef SIRENA_LWM2M_OBJECT_REGISTRY_H
#define SIRENA_LWM2M_OBJECT_REGISTRY_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

enum sirena_lwm2m_value_type {
    SIRENA_LWM2M_VALUE_NONE,
    SIRENA_LWM2M_VALUE_BOOLEAN,
    SIRENA_LWM2M_VALUE_INTEGER,
    SIRENA_LWM2M_VALUE_UNSIGNED,
    SIRENA_LWM2M_VALUE_STRING,
    SIRENA_LWM2M_VALUE_OPAQUE,
    SIRENA_LWM2M_VALUE_FLOAT,
    SIRENA_LWM2M_VALUE_MULTIPLE
};

#define SIRENA_LWM2M_OP_READ   (1U << 0)
#define SIRENA_LWM2M_OP_WRITE  (1U << 1)
#define SIRENA_LWM2M_OP_EXEC   (1U << 2)

typedef int (*sirena_lwm2m_read_fn)(uint16_t object_id, uint16_t instance_id,
				    uint16_t resource_id, void *user_data,
				    void *buffer, size_t *length);

typedef int (*sirena_lwm2m_write_fn)(uint16_t object_id, uint16_t instance_id,
				     uint16_t resource_id, void *user_data,
				     const void *buffer, size_t length);

typedef int (*sirena_lwm2m_execute_fn)(uint16_t object_id, uint16_t instance_id,
				       uint16_t resource_id, void *user_data,
				       const void *args, size_t args_length);

typedef int (*sirena_lwm2m_instance_visitor_t)(uint16_t object_id, uint16_t instance_id,
					       void *user_data);

struct sirena_lwm2m_resource {
    uint16_t id;
    enum sirena_lwm2m_value_type type;
    uint8_t operations;
    bool optional;
    void *user_data;
    sirena_lwm2m_read_fn read;
    sirena_lwm2m_write_fn write;
    sirena_lwm2m_execute_fn execute;
};

struct sirena_lwm2m_instance {
    uint16_t object_id;
    uint16_t instance_id;
    bool created;
    void *user_data;
    struct sirena_lwm2m_resource resources[CONFIG_SIRENA_LWM2M_MAX_RESOURCES_PER_INSTANCE];
    size_t resource_count;
};

struct sirena_lwm2m_object {
    uint16_t id;
    bool registered;
    bool optional;
};

int sirena_lwm2m_registry_init(void);
int sirena_lwm2m_register_object(uint16_t object_id, bool optional);
int sirena_lwm2m_create_instance(uint16_t object_id, uint16_t instance_id,
				 void *user_data);
int sirena_lwm2m_add_resource(uint16_t object_id, uint16_t instance_id,
			      uint16_t resource_id, enum sirena_lwm2m_value_type type,
			      uint8_t operations, bool optional,
			      sirena_lwm2m_read_fn read,
			      sirena_lwm2m_write_fn write,
			      sirena_lwm2m_execute_fn execute, void *user_data);
int sirena_lwm2m_get(uint16_t object_id, uint16_t instance_id,
		     uint16_t resource_id, void *buffer, size_t *length);
int sirena_lwm2m_set(uint16_t object_id, uint16_t instance_id,
		     uint16_t resource_id, const void *buffer, size_t length);
int sirena_lwm2m_execute(uint16_t object_id, uint16_t instance_id,
			 uint16_t resource_id, const void *args,
			 size_t args_length);
int sirena_lwm2m_find_instance(uint16_t object_id, uint16_t instance_id,
			       struct sirena_lwm2m_instance **instance);
int sirena_lwm2m_find_resource(uint16_t object_id, uint16_t instance_id,
			       uint16_t resource_id,
			       struct sirena_lwm2m_resource **resource);
int sirena_lwm2m_foreach_instance(sirena_lwm2m_instance_visitor_t visitor,
				  void *user_data);
bool sirena_lwm2m_object_enabled(uint16_t object_id);

#endif
