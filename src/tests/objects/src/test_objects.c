#include <zephyr/ztest.h>
#include <zephyr/sys/util.h>
#include <sirena/lwm2m/object_registry.h>
#include <sirena/lwm2m/objects.h>
#include <string.h>
#include <errno.h>

#define SECURITY_OBJECT_ID 0U
#define SERVER_OBJECT_ID 1U
#define DEVICE_OBJECT_ID 3U

#define GUARD_BYTE 0xA5

static const uint16_t security_mandatory[] = { 0U, 1U, 2U, 3U, 4U, 5U, 10U };
static const uint16_t server_mandatory[] = { 0U, 1U, 6U, 7U, 8U };
static const uint16_t device_mandatory[] = { 0U, 1U, 2U, 3U, 4U, 11U, 16U, 17U, 18U, 19U };

static const uint16_t security_optional[] = { 6U,  7U,  8U,  9U,  11U, 12U, 13U, 14U,
                                               15U, 17U, 22U, 23U, 24U, 26U, 28U, 29U, 30U };
static const uint16_t server_optional[] = { 2U, 3U, 4U, 5U };
static const uint16_t device_optional[] = { 5U,  9U,  10U, 12U, 13U,
                                           14U, 15U, 20U, 21U };

static int execute_calls;
static uint16_t execute_object;
static uint16_t execute_instance;
static uint16_t execute_resource;
static void *execute_user_data;

static void handle_execute(uint16_t object_id, uint16_t instance_id, uint16_t resource_id,
                           void *user_data)
{
    execute_calls++;
    execute_object = object_id;
    execute_instance = instance_id;
    execute_resource = resource_id;
    execute_user_data = user_data;
}

static void reset_module(void *fixture)
{
    ARG_UNUSED(fixture);

    zassert_equal(sirena_lwm2m_registry_init(), 0, "registry init failed");
    zassert_equal(sirena_lwm2m_objects_init(), 0, "objects init failed");
    sirena_lwm2m_set_execute_handler(NULL, NULL);
    execute_calls = 0;
    execute_object = 0U;
    execute_instance = 0U;
    execute_resource = 0U;
    execute_user_data = NULL;
}

static bool resource_present(uint16_t object_id, uint16_t resource_id)
{
    struct sirena_lwm2m_resource *resource;

    return sirena_lwm2m_find_resource(object_id, 0U, resource_id, &resource) == 0;
}

static void assert_resources_present(uint16_t object_id, const uint16_t *ids, size_t count)
{
    size_t i;

    for (i = 0U; i < count; ++i) {
        zassert_true(resource_present(object_id, ids[i]),
                     "object %u resource %u missing", object_id, ids[i]);
    }
}

ZTEST(sirena_lwm2m_objects, test_mandatory_resources_registered)
{
    struct sirena_lwm2m_instance *instance;

    zassert_equal(sirena_lwm2m_find_instance(SECURITY_OBJECT_ID, 0U, &instance), 0,
                  "security instance missing");
    zassert_equal(sirena_lwm2m_find_instance(SERVER_OBJECT_ID, 0U, &instance), 0,
                  "server instance missing");
    zassert_equal(sirena_lwm2m_find_instance(DEVICE_OBJECT_ID, 0U, &instance), 0,
                  "device instance missing");

    assert_resources_present(SECURITY_OBJECT_ID, security_mandatory,
                             ARRAY_SIZE(security_mandatory));
    assert_resources_present(SERVER_OBJECT_ID, server_mandatory,
                             ARRAY_SIZE(server_mandatory));
    assert_resources_present(DEVICE_OBJECT_ID, device_mandatory,
                             ARRAY_SIZE(device_mandatory));
}

ZTEST(sirena_lwm2m_objects, test_optional_resources_follow_configuration)
{
    size_t i;

    for (i = 0U; i < ARRAY_SIZE(security_optional); ++i) {
        zassert_equal(resource_present(SECURITY_OBJECT_ID, security_optional[i]),
                      IS_ENABLED(CONFIG_SIRENA_LWM2M_OBJECT_SECURITY_OPTIONAL),
                      "security resource %u configuration mismatch",
                      security_optional[i]);
    }

    for (i = 0U; i < ARRAY_SIZE(server_optional); ++i) {
        zassert_equal(resource_present(SERVER_OBJECT_ID, server_optional[i]),
                      IS_ENABLED(CONFIG_SIRENA_LWM2M_OBJECT_SERVER_OPTIONAL),
                      "server resource %u configuration mismatch", server_optional[i]);
    }

    for (i = 0U; i < ARRAY_SIZE(device_optional); ++i) {
        zassert_equal(resource_present(DEVICE_OBJECT_ID, device_optional[i]),
                      IS_ENABLED(CONFIG_SIRENA_LWM2M_OBJECT_DEVICE_OPTIONAL),
                      "device resource %u configuration mismatch", device_optional[i]);
    }
}

ZTEST(sirena_lwm2m_objects, test_object_enabled)
{
    zassert_true(sirena_lwm2m_object_enabled(SECURITY_OBJECT_ID), "security disabled");
    zassert_true(sirena_lwm2m_object_enabled(SERVER_OBJECT_ID), "server disabled");
    zassert_true(sirena_lwm2m_object_enabled(DEVICE_OBJECT_ID), "device disabled");

    zassert_false(sirena_lwm2m_object_enabled(2U), "object 2 enabled");
    zassert_false(sirena_lwm2m_object_enabled(4U), "object 4 enabled");
    zassert_false(sirena_lwm2m_object_enabled(5U), "object 5 enabled");
    zassert_false(sirena_lwm2m_object_enabled(99U), "object 99 enabled");
}

ZTEST(sirena_lwm2m_objects, test_typed_round_trip)
{
    char text[64];
    size_t length;
    uint8_t opaque[16];
    int64_t integer;
    bool boolean;
    int rc;

    rc = sirena_lwm2m_set_string(SECURITY_OBJECT_ID, 0U, 0U, "coaps://example.com");
    zassert_equal(rc, 0, "set string failed");
    length = sizeof(text);
    rc = sirena_lwm2m_get_string(SECURITY_OBJECT_ID, 0U, 0U, text, &length);
    zassert_equal(rc, 0, "get string failed");
    zassert_equal(length, strlen("coaps://example.com"), "string length mismatch");
    zassert_true(strcmp(text, "coaps://example.com") == 0, "string mismatch");

    rc = sirena_lwm2m_set_integer(SERVER_OBJECT_ID, 0U, 1U, 300);
    zassert_equal(rc, 0, "set integer failed");
    rc = sirena_lwm2m_get_integer(SERVER_OBJECT_ID, 0U, 1U, &integer);
    zassert_equal(rc, 0, "get integer failed");
    zassert_equal(integer, 300, "integer mismatch");

    rc = sirena_lwm2m_set_boolean(SERVER_OBJECT_ID, 0U, 6U, true);
    zassert_equal(rc, 0, "set boolean failed");
    rc = sirena_lwm2m_get_boolean(SERVER_OBJECT_ID, 0U, 6U, &boolean);
    zassert_equal(rc, 0, "get boolean failed");
    zassert_true(boolean, "boolean mismatch");

    rc = sirena_lwm2m_set_opaque(SECURITY_OBJECT_ID, 0U, 3U, "identity-bytes", 13U);
    zassert_equal(rc, 0, "set opaque failed");
    length = sizeof(opaque);
    rc = sirena_lwm2m_get_opaque(SECURITY_OBJECT_ID, 0U, 3U, opaque, &length);
    zassert_equal(rc, 0, "get opaque failed");
    zassert_equal(length, 13U, "opaque length mismatch");
    zassert_mem_equal(opaque, "identity-bytes", 13U, "opaque mismatch");
}

ZTEST(sirena_lwm2m_objects, test_type_mismatch_rejected)
{
    int rc;

    rc = sirena_lwm2m_set_string(SERVER_OBJECT_ID, 0U, 1U, "lifetime");
    zassert_equal(rc, -EINVAL, "string on integer accepted");

    rc = sirena_lwm2m_set_integer(SECURITY_OBJECT_ID, 0U, 0U, 5);
    zassert_equal(rc, -EINVAL, "integer on string accepted");

    rc = sirena_lwm2m_set_boolean(SECURITY_OBJECT_ID, 0U, 3U, true);
    zassert_equal(rc, -EINVAL, "boolean on opaque accepted");

    rc = sirena_lwm2m_set_opaque(SERVER_OBJECT_ID, 0U, 7U, "U", 1U);
    zassert_equal(rc, -EINVAL, "opaque on string accepted");
}

ZTEST(sirena_lwm2m_objects, test_unknown_targets_rejected)
{
    int64_t integer;
    int rc;

    rc = sirena_lwm2m_set_string(9U, 0U, 0U, "uri");
    zassert_equal(rc, -ENOENT, "unknown object accepted");

    rc = sirena_lwm2m_set_string(SECURITY_OBJECT_ID, 7U, 0U, "uri");
    zassert_equal(rc, -ENOENT, "unknown instance accepted");

    rc = sirena_lwm2m_set_string(SECURITY_OBJECT_ID, 0U, 99U, "uri");
    zassert_equal(rc, -ENOENT, "unknown resource accepted");

    rc = sirena_lwm2m_get_integer(9U, 0U, 0U, &integer);
    zassert_equal(rc, -ENOENT, "unknown object read accepted");
}

ZTEST(sirena_lwm2m_objects, test_capacity_overflow_rejected)
{
    static char oversized[CONFIG_SIRENA_LWM2M_SERVER_URI_MAX + 8U];
    uint8_t guarded[32];
    size_t small = 4U;
    int rc;

    memset(oversized, 'u', sizeof(oversized));

    rc = sirena_lwm2m_set_string(SECURITY_OBJECT_ID, 0U, 0U, oversized);
    zassert_equal(rc, -ENOMEM, "oversized string accepted");

    rc = sirena_lwm2m_set_opaque(SECURITY_OBJECT_ID, 0U, 3U, oversized, sizeof(oversized));
    zassert_equal(rc, -ENOMEM, "oversized opaque accepted");

    rc = sirena_lwm2m_set_string(SECURITY_OBJECT_ID, 0U, 0U, "coaps://example.com");
    zassert_equal(rc, 0, "set string failed");

    memset(guarded, GUARD_BYTE, sizeof(guarded));
    rc = sirena_lwm2m_get_string(SECURITY_OBJECT_ID, 0U, 0U, (char *)guarded, &small);
    zassert_equal(rc, -ENOMEM, "small buffer accepted");
    zassert_equal(guarded[0], GUARD_BYTE, "buffer overwritten");
    zassert_equal(guarded[1], GUARD_BYTE, "buffer overwritten");
    zassert_equal(guarded[2], GUARD_BYTE, "buffer overwritten");
    zassert_equal(guarded[3], GUARD_BYTE, "buffer overwritten");
}

ZTEST(sirena_lwm2m_objects, test_server_path_rejects_read_only_resources)
{
    int64_t value = 42;
    int rc;

    rc = sirena_lwm2m_set(SECURITY_OBJECT_ID, 0U, 0U, "coaps://example.com", 18U);
    zassert_equal(rc, -ENOTSUP, "server write to read-only resource accepted");

    rc = sirena_lwm2m_set(SERVER_OBJECT_ID, 0U, 0U, &value, sizeof(value));
    zassert_equal(rc, -ENOTSUP, "server write to read-only resource accepted");

    rc = sirena_lwm2m_set(SERVER_OBJECT_ID, 0U, 8U, &value, sizeof(value));
    zassert_equal(rc, -ENOTSUP, "server write to execute-only resource accepted");
}

ZTEST(sirena_lwm2m_objects, test_application_path_writes_read_only_resource)
{
    int64_t value = 42;
    int64_t read_back;
    size_t length;
    int rc;

    rc = sirena_lwm2m_set_integer(SERVER_OBJECT_ID, 0U, 0U, value);
    zassert_equal(rc, 0, "application write to read-only resource failed");

    rc = sirena_lwm2m_get_integer(SERVER_OBJECT_ID, 0U, 0U, &read_back);
    zassert_equal(rc, 0, "read back failed");
    zassert_equal(read_back, value, "stored value mismatch");

    rc = sirena_lwm2m_set_integer(SERVER_OBJECT_ID, 0U, 1U, 300);
    zassert_equal(rc, 0, "application write failed");

    length = sizeof(read_back);
    rc = sirena_lwm2m_get(SERVER_OBJECT_ID, 0U, 1U, &read_back, &length);
    zassert_equal(rc, 0, "server read failed");
    zassert_equal(length, sizeof(read_back), "server read length mismatch");
    zassert_equal(read_back, 300, "paths disagree on stored value");
}

ZTEST(sirena_lwm2m_objects, test_execute_only_resource_not_writable)
{
    int rc;

    rc = sirena_lwm2m_set_integer(SERVER_OBJECT_ID, 0U, 8U, 1);
    zassert_equal(rc, -ENOTSUP, "application write to execute-only resource accepted");

    rc = sirena_lwm2m_set_integer(DEVICE_OBJECT_ID, 0U, 4U, 1);
    zassert_equal(rc, -ENOTSUP, "application write to execute-only resource accepted");
}

ZTEST(sirena_lwm2m_objects, test_execute_handler_invoked)
{
    static int marker;
    int rc;

    rc = sirena_lwm2m_execute(SERVER_OBJECT_ID, 0U, 8U, NULL, 0U);
    zassert_equal(rc, -ENOTSUP, "execute without handler accepted");

    sirena_lwm2m_set_execute_handler(handle_execute, &marker);
    rc = sirena_lwm2m_execute(SERVER_OBJECT_ID, 0U, 8U, NULL, 0U);
    zassert_equal(rc, 0, "execute failed");
    zassert_equal(execute_calls, 1, "handler not called");
    zassert_equal(execute_object, SERVER_OBJECT_ID, "wrong object id");
    zassert_equal(execute_instance, 0U, "wrong instance id");
    zassert_equal(execute_resource, 8U, "wrong resource id");
    zassert_equal(execute_user_data, &marker, "wrong user data");

    rc = sirena_lwm2m_execute(DEVICE_OBJECT_ID, 0U, 4U, NULL, 0U);
    zassert_equal(rc, 0, "device execute failed");
    zassert_equal(execute_calls, 2, "handler not called");
    zassert_equal(execute_object, DEVICE_OBJECT_ID, "wrong object id");
    zassert_equal(execute_resource, 4U, "wrong resource id");
}

ZTEST(sirena_lwm2m_objects, test_bootstrap_server_reads_false)
{
    bool value = true;
    int rc;

    rc = sirena_lwm2m_get_boolean(SECURITY_OBJECT_ID, 0U, 1U, &value);
    zassert_equal(rc, 0, "get bootstrap flag failed");
    zassert_false(value, "bootstrap flag not false");
}

ZTEST(sirena_lwm2m_objects, test_provisioning_populates_objects)
{
    static const struct sirena_lwm2m_provisioning provisioning = {
        .server_uri = "coaps://example.com",
        .security_mode = SIRENA_LWM2M_SECURITY_MODE_PSK,
        .identity = "identity",
        .identity_length = 8U,
        .server_public_key = "public-key",
        .server_public_key_length = 10U,
        .secret_key = "secret",
        .secret_key_length = 6U,
        .short_server_id = 42,
        .lifetime = 300,
        .binding = "U",
    };
    uint8_t opaque[16];
    char text[64];
    size_t length;
    int64_t integer;
    bool value;
    int rc;

    rc = sirena_lwm2m_provision(&provisioning);
    zassert_equal(rc, 0, "provisioning failed");

    length = sizeof(text);
    rc = sirena_lwm2m_get_string(SECURITY_OBJECT_ID, 0U, 0U, text, &length);
    zassert_equal(rc, 0, "get uri failed");
    zassert_true(strcmp(text, "coaps://example.com") == 0, "uri mismatch");

    rc = sirena_lwm2m_get_integer(SECURITY_OBJECT_ID, 0U, 2U, &integer);
    zassert_equal(rc, 0, "get mode failed");
    zassert_equal(integer, SIRENA_LWM2M_SECURITY_MODE_PSK, "mode mismatch");

    length = sizeof(opaque);
    rc = sirena_lwm2m_get_opaque(SECURITY_OBJECT_ID, 0U, 3U, opaque, &length);
    zassert_equal(rc, 0, "get identity failed");
    zassert_equal(length, 8U, "identity length mismatch");
    zassert_mem_equal(opaque, "identity", 8U, "identity mismatch");

    length = sizeof(opaque);
    rc = sirena_lwm2m_get_opaque(SECURITY_OBJECT_ID, 0U, 4U, opaque, &length);
    zassert_equal(rc, 0, "get public key failed");
    zassert_equal(length, 10U, "public key length mismatch");

    length = sizeof(opaque);
    rc = sirena_lwm2m_get_opaque(SECURITY_OBJECT_ID, 0U, 5U, opaque, &length);
    zassert_equal(rc, 0, "get secret key failed");
    zassert_equal(length, 6U, "secret key mismatch");

    rc = sirena_lwm2m_get_integer(SECURITY_OBJECT_ID, 0U, 10U, &integer);
    zassert_equal(rc, 0, "get security short server id failed");
    zassert_equal(integer, 42, "security short server id mismatch");

    rc = sirena_lwm2m_get_integer(SERVER_OBJECT_ID, 0U, 0U, &integer);
    zassert_equal(rc, 0, "get server short server id failed");
    zassert_equal(integer, 42, "server short server id mismatch");

    rc = sirena_lwm2m_get_integer(SERVER_OBJECT_ID, 0U, 1U, &integer);
    zassert_equal(rc, 0, "get lifetime failed");
    zassert_equal(integer, 300, "lifetime mismatch");

    length = sizeof(text);
    rc = sirena_lwm2m_get_string(SERVER_OBJECT_ID, 0U, 7U, text, &length);
    zassert_equal(rc, 0, "get binding failed");
    zassert_true(strcmp(text, "U") == 0, "binding mismatch");

    rc = sirena_lwm2m_get_boolean(SECURITY_OBJECT_ID, 0U, 1U, &value);
    zassert_equal(rc, 0, "get bootstrap flag failed");
    zassert_false(value, "bootstrap flag not false");
}

ZTEST(sirena_lwm2m_objects, test_provisioning_rejects_invalid_input)
{
    struct sirena_lwm2m_provisioning provisioning = {
        .server_uri = "coaps://example.com",
        .security_mode = SIRENA_LWM2M_SECURITY_MODE_NOSEC,
        .short_server_id = 1,
        .lifetime = 300,
        .binding = "U",
    };
    int rc;

    rc = sirena_lwm2m_provision(NULL);
    zassert_equal(rc, -EINVAL, "null provisioning accepted");

    provisioning.security_mode = 4;
    rc = sirena_lwm2m_provision(&provisioning);
    zassert_equal(rc, -EINVAL, "unsupported security mode accepted");

    provisioning.security_mode = SIRENA_LWM2M_SECURITY_MODE_RPK;
    rc = sirena_lwm2m_provision(&provisioning);
    zassert_equal(rc, 0, "supported security mode rejected");
}

ZTEST_SUITE(sirena_lwm2m_objects, NULL, NULL, reset_module, NULL, NULL);
