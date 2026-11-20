#include <sirena/lwm2m/object_registry.h>
#include <string.h>
#include <errno.h>

static struct sirena_lwm2m_object objects[CONFIG_SIRENA_LWM2M_MAX_OBJECTS];
static struct sirena_lwm2m_instance instances[CONFIG_SIRENA_LWM2M_MAX_INSTANCES];

static struct sirena_lwm2m_object *find_object(uint16_t object_id)
{
    size_t i;

    for (i = 0U; i < CONFIG_SIRENA_LWM2M_MAX_OBJECTS; ++i) {
        if (objects[i].registered && objects[i].id == object_id) {
            return &objects[i];
        }
    }

    return NULL;
}

static struct sirena_lwm2m_instance *find_instance_storage(uint16_t object_id,
							    uint16_t instance_id)
{
    size_t i;

    for (i = 0U; i < CONFIG_SIRENA_LWM2M_MAX_INSTANCES; ++i) {
        if (instances[i].created && instances[i].object_id == object_id &&
            instances[i].instance_id == instance_id) {
            return &instances[i];
        }
    }

    return NULL;
}

static struct sirena_lwm2m_resource *find_resource(struct sirena_lwm2m_instance *inst,
                                                   uint16_t resource_id)
{
    size_t i;

    if (inst == NULL) {
        return NULL;
    }

    for (i = 0U; i < inst->resource_count; ++i) {
        if (inst->resources[i].id == resource_id) {
            return &inst->resources[i];
        }
    }

    return NULL;
}

int sirena_lwm2m_registry_init(void)
{
    memset(objects, 0, sizeof(objects));
    memset(instances, 0, sizeof(instances));
    return 0;
}

bool sirena_lwm2m_object_enabled(uint16_t object_id)
{
    switch (object_id) {
    case 0:
        return IS_ENABLED(CONFIG_SIRENA_LWM2M_OBJECT_SECURITY);
    case 1:
        return IS_ENABLED(CONFIG_SIRENA_LWM2M_OBJECT_SERVER);
    case 3:
        return IS_ENABLED(CONFIG_SIRENA_LWM2M_OBJECT_DEVICE);
    default:
        return true;
    }
}

int sirena_lwm2m_register_object(uint16_t object_id, bool optional)
{
    size_t i;

    if (find_object(object_id) != NULL) {
        return 0;
    }

    if (optional && !sirena_lwm2m_object_enabled(object_id)) {
        return -ENOTSUP;
    }

    for (i = 0U; i < CONFIG_SIRENA_LWM2M_MAX_OBJECTS; ++i) {
        if (!objects[i].registered) {
            objects[i].id = object_id;
            objects[i].registered = true;
            objects[i].optional = optional;
            return 0;
        }
    }

    return -ENOMEM;
}

int sirena_lwm2m_create_instance(uint16_t object_id, uint16_t instance_id,
				 void *user_data)
{
    struct sirena_lwm2m_object *obj;
    size_t i;

    obj = find_object(object_id);
    if (obj == NULL) {
        return -ENOENT;
    }

    if (find_instance_storage(object_id, instance_id) != NULL) {
        return -EEXIST;
    }

    for (i = 0U; i < CONFIG_SIRENA_LWM2M_MAX_INSTANCES; ++i) {
        if (!instances[i].created) {
            instances[i].object_id = object_id;
            instances[i].instance_id = instance_id;
            instances[i].created = true;
            instances[i].user_data = user_data;
            instances[i].resource_count = 0U;
            return 0;
        }
    }

    return -ENOMEM;
}

int sirena_lwm2m_add_resource(uint16_t object_id, uint16_t instance_id,
			      uint16_t resource_id,
			      enum sirena_lwm2m_value_type type,
			      uint8_t operations, bool optional,
			      sirena_lwm2m_read_fn read,
			      sirena_lwm2m_write_fn write,
			      sirena_lwm2m_execute_fn execute,
			      void *user_data)
{
    struct sirena_lwm2m_instance *inst;
    struct sirena_lwm2m_resource *res;

    inst = find_instance_storage(object_id, instance_id);
    if (inst == NULL) {
        return -ENOENT;
    }

    if (optional && !sirena_lwm2m_object_enabled(object_id)) {
        return -ENOTSUP;
    }

    if (find_resource(inst, resource_id) != NULL) {
        return -EEXIST;
    }

    if (inst->resource_count >= CONFIG_SIRENA_LWM2M_MAX_RESOURCES_PER_INSTANCE) {
        return -ENOMEM;
    }

    res = &inst->resources[inst->resource_count];
    memset(res, 0, sizeof(*res));
    res->id = resource_id;
    res->type = type;
    res->operations = operations;
    res->optional = optional;
    res->user_data = user_data;
    res->read = read;
    res->write = write;
    res->execute = execute;
    inst->resource_count += 1U;

    return 0;
}

int sirena_lwm2m_find_instance(uint16_t object_id, uint16_t instance_id,
			       struct sirena_lwm2m_instance **instance)
{
    struct sirena_lwm2m_instance *inst;

    inst = find_instance_storage(object_id, instance_id);
    if (inst == NULL) {
        return -ENOENT;
    }

    if (instance != NULL) {
        *instance = inst;
    }

    return 0;
}

int sirena_lwm2m_get(uint16_t object_id, uint16_t instance_id,
		     uint16_t resource_id, void *buffer, size_t *length)
{
    struct sirena_lwm2m_instance *inst;
    struct sirena_lwm2m_resource *res;

    inst = find_instance_storage(object_id, instance_id);
    if (inst == NULL) {
        return -ENOENT;
    }

    res = find_resource(inst, resource_id);
    if (res == NULL) {
        return -ENOENT;
    }

    size_t len = (length != NULL) ? *length : 0U;
    int rc = 0;

    if (!(res->operations & SIRENA_LWM2M_OP_READ) || res->read == NULL) {
        return -ENOTSUP;
    }

    rc = res->read(object_id, instance_id, resource_id, res->user_data,
		     buffer, &len);
    if (rc == 0 && length != NULL) {
        if (len > *length) {
            return -EOVERFLOW;
        }
        *length = len;
    }

    return rc;
}

int sirena_lwm2m_set(uint16_t object_id, uint16_t instance_id,
		     uint16_t resource_id, const void *buffer, size_t length)
{
    struct sirena_lwm2m_instance *inst;
    struct sirena_lwm2m_resource *res;

    inst = find_instance_storage(object_id, instance_id);
    if (inst == NULL) {
        return -ENOENT;
    }

    res = find_resource(inst, resource_id);
    if (res == NULL) {
        return -ENOENT;
    }

    if (!(res->operations & SIRENA_LWM2M_OP_WRITE) || res->write == NULL) {
        return -ENOTSUP;
    }

    return res->write(object_id, instance_id, resource_id, res->user_data,
		      buffer, length);
}

int sirena_lwm2m_execute(uint16_t object_id, uint16_t instance_id,
			 uint16_t resource_id, const void *args,
			 size_t args_length)
{
    struct sirena_lwm2m_instance *inst;
    struct sirena_lwm2m_resource *res;

    inst = find_instance_storage(object_id, instance_id);
    if (inst == NULL) {
        return -ENOENT;
    }

    res = find_resource(inst, resource_id);
    if (res == NULL) {
        return -ENOENT;
    }

    if (!(res->operations & SIRENA_LWM2M_OP_EXEC) || res->execute == NULL) {
        return -ENOTSUP;
    }

    return res->execute(object_id, instance_id, resource_id, res->user_data,
			args, args_length);
}
