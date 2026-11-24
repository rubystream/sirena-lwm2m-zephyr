#include <sirena/lwm2m/object_registry.h>
#include <string.h>
#include <errno.h>
#include <zephyr/kernel.h>

static struct sirena_lwm2m_object objects[CONFIG_SIRENA_LWM2M_MAX_OBJECTS];
static struct sirena_lwm2m_instance instances[CONFIG_SIRENA_LWM2M_MAX_INSTANCES];

K_MUTEX_DEFINE(sirena_lwm2m_registry_lock);

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
    k_mutex_lock(&sirena_lwm2m_registry_lock, K_FOREVER);
    memset(objects, 0, sizeof(objects));
    memset(instances, 0, sizeof(instances));
    k_mutex_unlock(&sirena_lwm2m_registry_lock);
    return 0;
}

bool sirena_lwm2m_object_enabled(uint16_t object_id)
{
    bool enabled;

    k_mutex_lock(&sirena_lwm2m_registry_lock, K_FOREVER);
    switch (object_id) {
    case 0:
    case 1:
    case 3:
        enabled = true;
        break;
    default:
        enabled = false;
        break;
    }
    k_mutex_unlock(&sirena_lwm2m_registry_lock);
    return enabled;
}

int sirena_lwm2m_register_object(uint16_t object_id, bool optional)
{
    size_t i;
    int rc = -ENOMEM;

    k_mutex_lock(&sirena_lwm2m_registry_lock, K_FOREVER);

    if (find_object(object_id) != NULL) {
        rc = 0;
        goto done;
    }

    if (!sirena_lwm2m_object_enabled(object_id)) {
        rc = -ENOTSUP;
        goto done;
    }

    for (i = 0U; i < CONFIG_SIRENA_LWM2M_MAX_OBJECTS; ++i) {
        if (!objects[i].registered) {
            objects[i].id = object_id;
            objects[i].registered = true;
            objects[i].optional = optional;
            rc = 0;
            goto done;
        }
    }

done:
    k_mutex_unlock(&sirena_lwm2m_registry_lock);
    return rc;
}

int sirena_lwm2m_create_instance(uint16_t object_id, uint16_t instance_id,
				 void *user_data)
{
    struct sirena_lwm2m_object *obj;
    size_t i;
    int rc = -ENOMEM;

    k_mutex_lock(&sirena_lwm2m_registry_lock, K_FOREVER);

    obj = find_object(object_id);
    if (obj == NULL) {
        rc = -ENOENT;
        goto done;
    }

    if (find_instance_storage(object_id, instance_id) != NULL) {
        rc = -EEXIST;
        goto done;
    }

    for (i = 0U; i < CONFIG_SIRENA_LWM2M_MAX_INSTANCES; ++i) {
        if (!instances[i].created) {
            memset(&instances[i], 0, sizeof(instances[i]));
            instances[i].object_id = object_id;
            instances[i].instance_id = instance_id;
            instances[i].created = true;
            instances[i].user_data = user_data;
            instances[i].resource_count = 0U;
            rc = 0;
            goto done;
        }
    }

done:
    k_mutex_unlock(&sirena_lwm2m_registry_lock);
    return rc;
}

int sirena_lwm2m_delete_instance(uint16_t object_id, uint16_t instance_id)
{
    struct sirena_lwm2m_instance *inst;
    int rc = -ENOENT;

    k_mutex_lock(&sirena_lwm2m_registry_lock, K_FOREVER);
    inst = find_instance_storage(object_id, instance_id);
    if (inst == NULL) {
        goto done;
    }
    memset(inst, 0, sizeof(*inst));
    rc = 0;
done:
    k_mutex_unlock(&sirena_lwm2m_registry_lock);
    return rc;
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
    int rc = -ENOENT;

    k_mutex_lock(&sirena_lwm2m_registry_lock, K_FOREVER);

    inst = find_instance_storage(object_id, instance_id);
    if (inst == NULL) {
        goto done;
    }

    if (optional && !sirena_lwm2m_object_enabled(object_id)) {
        rc = -ENOTSUP;
        goto done;
    }

    if (find_resource(inst, resource_id) != NULL) {
        rc = -EEXIST;
        goto done;
    }

    if (inst->resource_count >= CONFIG_SIRENA_LWM2M_MAX_RESOURCES_PER_INSTANCE) {
        rc = -ENOMEM;
        goto done;
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
    rc = 0;

done:
    k_mutex_unlock(&sirena_lwm2m_registry_lock);
    return rc;
}

int sirena_lwm2m_find_instance(uint16_t object_id, uint16_t instance_id,
			       struct sirena_lwm2m_instance **instance)
{
    struct sirena_lwm2m_instance *inst;
    int rc = -ENOENT;

    k_mutex_lock(&sirena_lwm2m_registry_lock, K_FOREVER);
    inst = find_instance_storage(object_id, instance_id);
    if (inst == NULL) {
        goto done;
    }
    if (instance != NULL) {
        *instance = inst;
    }
    rc = 0;
done:
    k_mutex_unlock(&sirena_lwm2m_registry_lock);
    return rc;
}

int sirena_lwm2m_find_resource(uint16_t object_id, uint16_t instance_id,
			       uint16_t resource_id,
			       struct sirena_lwm2m_resource **resource)
{
    struct sirena_lwm2m_instance *inst;
    struct sirena_lwm2m_resource *res;
    int rc = -ENOENT;

    k_mutex_lock(&sirena_lwm2m_registry_lock, K_FOREVER);
    inst = find_instance_storage(object_id, instance_id);
    if (inst == NULL) {
        goto done;
    }
    res = find_resource(inst, resource_id);
    if (res == NULL) {
        goto done;
    }
    if (resource != NULL) {
        *resource = res;
    }
    rc = 0;
done:
    k_mutex_unlock(&sirena_lwm2m_registry_lock);
    return rc;
}

int sirena_lwm2m_foreach_instance(sirena_lwm2m_instance_visitor_t visitor,
				  void *user_data)
{
    size_t i;
    int rc = 0;

    if (visitor == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&sirena_lwm2m_registry_lock, K_FOREVER);
    for (i = 0U; i < CONFIG_SIRENA_LWM2M_MAX_INSTANCES; ++i) {
        if (!instances[i].created) {
            continue;
        }
        rc = visitor(instances[i].object_id, instances[i].instance_id, user_data);
        if (rc != 0) {
            break;
        }
    }
    k_mutex_unlock(&sirena_lwm2m_registry_lock);
    return rc;
}

int sirena_lwm2m_get(uint16_t object_id, uint16_t instance_id,
		     uint16_t resource_id, void *buffer, size_t *length)
{
    struct sirena_lwm2m_instance *inst;
    struct sirena_lwm2m_resource *res;
    int rc = -ENOENT;
    int call_rc = 0;

    k_mutex_lock(&sirena_lwm2m_registry_lock, K_FOREVER);
    inst = find_instance_storage(object_id, instance_id);
    if (inst == NULL) {
        goto done;
    }
    res = find_resource(inst, resource_id);
    if (res == NULL) {
        goto done;
    }
    if (!(res->operations & SIRENA_LWM2M_OP_READ) || res->read == NULL) {
        rc = -ENOTSUP;
        goto done;
    }

    size_t len = (length != NULL) ? *length : 0U;

    k_mutex_unlock(&sirena_lwm2m_registry_lock);
    call_rc = res->read(object_id, instance_id, resource_id, res->user_data,
			buffer, &len);
    k_mutex_lock(&sirena_lwm2m_registry_lock, K_FOREVER);

    if (call_rc != 0) {
        rc = call_rc;
        goto done;
    }
    if (length != NULL) {
        if (len > *length) {
            rc = -EOVERFLOW;
            goto done;
        }
        *length = len;
    }
    rc = 0;

done:
    k_mutex_unlock(&sirena_lwm2m_registry_lock);
    return rc;
}

int sirena_lwm2m_set(uint16_t object_id, uint16_t instance_id,
		     uint16_t resource_id, const void *buffer, size_t length)
{
    struct sirena_lwm2m_instance *inst;
    struct sirena_lwm2m_resource *res;
    int rc = -ENOENT;

    k_mutex_lock(&sirena_lwm2m_registry_lock, K_FOREVER);
    inst = find_instance_storage(object_id, instance_id);
    if (inst == NULL) {
        goto done_unlock;
    }
    res = find_resource(inst, resource_id);
    if (res == NULL) {
        goto done_unlock;
    }
    if (!(res->operations & SIRENA_LWM2M_OP_WRITE) || res->write == NULL) {
        rc = -ENOTSUP;
        goto done_unlock;
    }
    k_mutex_unlock(&sirena_lwm2m_registry_lock);
    rc = res->write(object_id, instance_id, resource_id, res->user_data,
		    buffer, length);
    return rc;

done_unlock:
    k_mutex_unlock(&sirena_lwm2m_registry_lock);
    return rc;
}

int sirena_lwm2m_execute(uint16_t object_id, uint16_t instance_id,
			 uint16_t resource_id, const void *args,
			 size_t args_length)
{
    struct sirena_lwm2m_instance *inst;
    struct sirena_lwm2m_resource *res;
    int rc = -ENOENT;

    k_mutex_lock(&sirena_lwm2m_registry_lock, K_FOREVER);
    inst = find_instance_storage(object_id, instance_id);
    if (inst == NULL) {
        goto done_unlock;
    }
    res = find_resource(inst, resource_id);
    if (res == NULL) {
        goto done_unlock;
    }
    if (!(res->operations & SIRENA_LWM2M_OP_EXEC) || res->execute == NULL) {
        rc = -ENOTSUP;
        goto done_unlock;
    }
    k_mutex_unlock(&sirena_lwm2m_registry_lock);
    rc = res->execute(object_id, instance_id, resource_id, res->user_data,
		      args, args_length);
    return rc;

done_unlock:
    k_mutex_unlock(&sirena_lwm2m_registry_lock);
    return rc;
}

int sirena_lwm2m_get_resource_attributes(uint16_t object_id, uint16_t instance_id,
				       uint16_t resource_id,
				       struct sirena_lwm2m_resource_attributes *attrs)
{
    struct sirena_lwm2m_instance *inst;
    struct sirena_lwm2m_resource *res;
    int rc = -ENOENT;

    if (attrs == NULL) {
        return -EINVAL;
    }

    k_mutex_lock(&sirena_lwm2m_registry_lock, K_FOREVER);
    inst = find_instance_storage(object_id, instance_id);
    if (inst == NULL) {
        goto done;
    }
    res = find_resource(inst, resource_id);
    if (res == NULL) {
        goto done;
    }
    *attrs = res->attributes;
    rc = 0;

done:
    k_mutex_unlock(&sirena_lwm2m_registry_lock);
    return rc;
}

int sirena_lwm2m_set_resource_attributes(uint16_t object_id, uint16_t instance_id,
				       uint16_t resource_id,
				       const struct sirena_lwm2m_resource_attributes *attrs)
{
    struct sirena_lwm2m_instance *inst;
    struct sirena_lwm2m_resource *res;
    int rc = -ENOENT;

    if (attrs == NULL) {
        return -EINVAL;
    }

    if (attrs->has_pmin && attrs->has_pmax && attrs->pmin > attrs->pmax) {
        return -EINVAL;
    }

    k_mutex_lock(&sirena_lwm2m_registry_lock, K_FOREVER);
    inst = find_instance_storage(object_id, instance_id);
    if (inst == NULL) {
        goto done;
    }
    res = find_resource(inst, resource_id);
    if (res == NULL) {
        goto done;
    }
    res->attributes = *attrs;
    rc = 0;

done:
    k_mutex_unlock(&sirena_lwm2m_registry_lock);
    return rc;
}
