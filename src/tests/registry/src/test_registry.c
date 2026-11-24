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

ZTEST(sirena_lwm2m_registry, test_resource_attributes)
{
    struct sirena_lwm2m_resource_attributes in = {0};
    struct sirena_lwm2m_resource_attributes out = {0};
    int rc;

    rc = sirena_lwm2m_registry_init();
    zassert_equal(rc, 0, "init failed");
    rc = sirena_lwm2m_register_object(0, false);
    zassert_equal(rc, 0, "register failed");
    rc = sirena_lwm2m_create_instance(0, 0, NULL);
    zassert_equal(rc, 0, "instance failed");
    rc = sirena_lwm2m_add_resource(0, 0, 7, SIRENA_LWM2M_VALUE_INTEGER,
                                    SIRENA_LWM2M_OP_READ, false, read_value, NULL,
                                    NULL, NULL);
    zassert_equal(rc, 0, "resource failed");

    memset(&in, 0, sizeof(in));
    in.has_pmin = true;
    in.pmin = 10;
    in.has_pmax = true;
    in.pmax = 20;
    in.has_gt = true;
    in.gt = 3.5;
    in.has_lt = true;
    in.lt = 1.25;
    in.has_st = true;
    in.st = 0.5;

    rc = sirena_lwm2m_set_resource_attributes(0, 0, 7, &in);
    zassert_equal(rc, 0, "set attrs failed");
    rc = sirena_lwm2m_get_resource_attributes(0, 0, 7, &out);
    zassert_equal(rc, 0, "get attrs failed");
    zassert_true(out.has_pmin, "pmin missing");
    zassert_equal(out.pmin, 10U, "pmin mismatch");
    zassert_true(out.has_pmax, "pmax missing");
    zassert_equal(out.pmax, 20U, "pmax mismatch");
    zassert_true(out.has_gt, "gt missing");
    zassert_true(out.gt == 3.5, "gt mismatch");
    zassert_true(out.has_lt, "lt missing");
    zassert_true(out.lt == 1.25, "lt mismatch");
    zassert_true(out.has_st, "st missing");
    zassert_true(out.st == 0.5, "st mismatch");

    in.pmin = 30;
    rc = sirena_lwm2m_set_resource_attributes(0, 0, 7, &in);
    zassert_equal(rc, -EINVAL, "invalid range accepted");
    rc = sirena_lwm2m_get_resource_attributes(0, 0, 7, &out);
    zassert_equal(rc, 0, "get after invalid failed");
    zassert_equal(out.pmin, 10U, "attributes changed by invalid write");

    in.pmin = 5;
    rc = sirena_lwm2m_set_resource_attributes(99, 0, 7, &in);
    zassert_equal(rc, -ENOENT, "missing object should fail");
    rc = sirena_lwm2m_set_resource_attributes(0, 0, 7, NULL);
    zassert_equal(rc, -EINVAL, "null set should fail");
    rc = sirena_lwm2m_get_resource_attributes(0, 0, 7, NULL);
    zassert_equal(rc, -EINVAL, "null get should fail");

    stored_value = 0;
    (void)stored_value;
}

ZTEST(sirena_lwm2m_registry, test_resource_attributes_persist)
{
    struct sirena_lwm2m_resource_attributes in = {0};
    struct sirena_lwm2m_resource_attributes out = {0};
    int rc;

    rc = sirena_lwm2m_registry_init();
    zassert_equal(rc, 0, "init failed");
    rc = sirena_lwm2m_register_object(0, false);
    zassert_equal(rc, 0, "register failed");
    rc = sirena_lwm2m_create_instance(0, 0, NULL);
    zassert_equal(rc, 0, "instance failed");
    rc = sirena_lwm2m_add_resource(0, 0, 9, SIRENA_LWM2M_VALUE_INTEGER,
                                    SIRENA_LWM2M_OP_READ, false, read_value, NULL,
                                    NULL, NULL);
    zassert_equal(rc, 0, "resource failed");

    in.has_pmin = true;
    in.pmin = 5;
    rc = sirena_lwm2m_set_resource_attributes(0, 0, 9, &in);
    zassert_equal(rc, 0, "set attrs failed");

    rc = sirena_lwm2m_delete_instance(0, 0);
    zassert_equal(rc, 0, "delete failed");
    rc = sirena_lwm2m_create_instance(0, 0, NULL);
    zassert_equal(rc, 0, "recreate failed");
    rc = sirena_lwm2m_add_resource(0, 0, 9, SIRENA_LWM2M_VALUE_INTEGER,
                                    SIRENA_LWM2M_OP_READ, false, read_value, NULL,
                                    NULL, NULL);
    zassert_equal(rc, 0, "readd failed");
    rc = sirena_lwm2m_get_resource_attributes(0, 0, 9, &out);
    zassert_equal(rc, 0, "get after re-register failed");
    zassert_false(out.has_pmin, "attrs persisted incorrectly");
}
