#include <zephyr/ztest.h>
#include <sirena/lwm2m/object_registry.h>
#include <string.h>

static int stored_value;
static bool executed;

static int read_value(uint16_t object_id, uint16_t instance_id,
                      uint16_t resource_id, void *user_data,
                      void *buffer, size_t *length)
{
    (void)object_id;
    (void)instance_id;
    (void)resource_id;
    (void)user_data;
    if (length != NULL && *length < sizeof(stored_value)) {
        return -ENOMEM;
    }
    memcpy(buffer, &stored_value, sizeof(stored_value));
    if (length != NULL) {
        *length = sizeof(stored_value);
    }
    return 0;
}

static int write_value(uint16_t object_id, uint16_t instance_id,
                       uint16_t resource_id, void *user_data,
                       const void *buffer, size_t length)
{
    (void)object_id;
    (void)instance_id;
    (void)resource_id;
    (void)user_data;
    if (length < sizeof(int)) {
        return -EINVAL;
    }
    memcpy(&stored_value, buffer, sizeof(stored_value));
    return 0;
}

static int run_execute(uint16_t object_id, uint16_t instance_id,
                       uint16_t resource_id, void *user_data,
                       const void *args, size_t args_length)
{
    (void)object_id;
    (void)instance_id;
    (void)resource_id;
    (void)user_data;
    (void)args;
    (void)args_length;
    executed = true;
    return 0;
}

ZTEST(sirena_lwm2m_registry, test_paths)
{
    int value;
    size_t length;
    int rc;

    stored_value = 0;
    executed = false;

    rc = sirena_lwm2m_registry_init();
    zassert_equal(rc, 0, "init failed");
    rc = sirena_lwm2m_register_object(0, false);
    zassert_equal(rc, 0, "register failed");
    rc = sirena_lwm2m_create_instance(0, 0, NULL);
    zassert_equal(rc, 0, "instance failed");
    rc = sirena_lwm2m_add_resource(0, 0, 5, SIRENA_LWM2M_VALUE_INTEGER,
                                   SIRENA_LWM2M_OP_READ | SIRENA_LWM2M_OP_WRITE |
                                       SIRENA_LWM2M_OP_EXEC,
                                   false, read_value, write_value, run_execute,
                                   NULL);
    zassert_equal(rc, 0, "resource failed");

    value = 42;
    rc = sirena_lwm2m_set(0, 0, 5, &value, sizeof(value));
    zassert_equal(rc, 0, "set failed");
    zassert_equal(stored_value, 42, "stored mismatch");

    length = sizeof(value);
    rc = sirena_lwm2m_get(0, 0, 5, &value, &length);
    zassert_equal(rc, 0, "get failed");
    zassert_equal(value, 42, "value mismatch");
    zassert_equal(length, sizeof(int), "length mismatch");

    rc = sirena_lwm2m_execute(0, 0, 5, NULL, 0);
    zassert_equal(rc, 0, "execute failed");
    zassert_true(executed, "execute not called");

    rc = sirena_lwm2m_get(99, 0, 5, &value, &length);
    zassert_equal(rc, -ENOENT, "missing object expected");
}

ZTEST_SUITE(sirena_lwm2m_registry, NULL, NULL, NULL, NULL, NULL);
