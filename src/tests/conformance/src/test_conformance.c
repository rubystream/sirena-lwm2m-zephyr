#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <sirena/lwm2m/object_registry.h>
#include <sirena/lwm2m/objects.h>
#include <sirena/lwm2m/protocol.h>
#include <sirena/lwm2m/transport.h>
#include <string.h>

#define HARNESS_OBJECT_ID 3U
#define HARNESS_INSTANCE_ID 0U
#define HARNESS_MANUFACTURER_RESOURCE_ID 0U
#define HARNESS_MODEL_RESOURCE_ID 1U
#define HARNESS_SERIAL_RESOURCE_ID 2U
#define HARNESS_FIRMWARE_RESOURCE_ID 3U
#define HARNESS_REBOOT_RESOURCE_ID 4U
#define HARNESS_BATTERY_RESOURCE_ID 9U
#define HARNESS_UTC_OFFSET_RESOURCE_ID 14U
#define HARNESS_RETRY_INTERVAL_MS 500
#define HARNESS_UTC_OFFSET_MAX 64U

static K_MUTEX_DEFINE(harness_lock);
static bool harness_command_handled;
static uint16_t harness_command_object_id;
static uint16_t harness_command_instance_id;
static uint16_t harness_command_resource_id;
static uint32_t harness_notifications;

static void harness_execute_handler(uint16_t object_id, uint16_t instance_id,
                                   uint16_t resource_id, void *user_data)
{
	(void)user_data;

	k_mutex_lock(&harness_lock, K_FOREVER);
	harness_command_handled = true;
	harness_command_object_id = object_id;
	harness_command_instance_id = instance_id;
	harness_command_resource_id = resource_id;
	k_mutex_unlock(&harness_lock);

	printk("sirena lwm2m conformance harness: the command %u/%u/%u was received\n", object_id,
	       instance_id, resource_id);
}

static bool harness_command_seen(uint16_t *object_id, uint16_t *instance_id,
				 uint16_t *resource_id)
{
	bool seen;

	k_mutex_lock(&harness_lock, K_FOREVER);
	seen = harness_command_handled;
	if (seen) {
		*object_id = harness_command_object_id;
		*instance_id = harness_command_instance_id;
		*resource_id = harness_command_resource_id;
	}
	k_mutex_unlock(&harness_lock);

	return seen;
}

static uint32_t harness_notification_count(void)
{
	uint32_t count;

	k_mutex_lock(&harness_lock, K_FOREVER);
	count = harness_notifications;
	k_mutex_unlock(&harness_lock);

	return count;
}

static int harness_set_device_string(uint16_t resource_id, const char *value)
{
	return sirena_lwm2m_set_string(HARNESS_OBJECT_ID, HARNESS_INSTANCE_ID, resource_id, value);
}

static int harness_prepare(void)
{
	struct sirena_lwm2m_provisioning provisioning = {
		.server_uri = CONFIG_SIRENA_LWM2M_CONFORMANCE_SERVER_URI,
		.security_mode = SIRENA_LWM2M_SECURITY_MODE_NOSEC,
		.short_server_id = 1U,
		.lifetime = CONFIG_SIRENA_LWM2M_CONFORMANCE_LIFETIME,
		.binding = CONFIG_SIRENA_LWM2M_CONFORMANCE_BINDING,
	};
	int rc;

	rc = sirena_lwm2m_registry_init();
	if (rc != 0) {
		printk("sirena lwm2m conformance harness: the registry failed: %d\n", rc);
		return rc;
	}

	rc = sirena_lwm2m_objects_init();
	if (rc != 0) {
		printk("sirena lwm2m conformance harness: the objects failed: %d\n", rc);
		return rc;
	}

	rc = sirena_lwm2m_provision(&provisioning);
	if (rc != 0) {
		printk("sirena lwm2m conformance harness: the provisioning failed: %d\n", rc);
		return rc;
	}

	rc = harness_set_device_string(HARNESS_MANUFACTURER_RESOURCE_ID, "Sirena");
	if (rc != 0) {
		return rc;
	}

	rc = harness_set_device_string(HARNESS_MODEL_RESOURCE_ID, "sirena-conformance");
	if (rc != 0) {
		return rc;
	}

	rc = harness_set_device_string(HARNESS_SERIAL_RESOURCE_ID, "sirena-conformance-0001");
	if (rc != 0) {
		return rc;
	}

	rc = harness_set_device_string(HARNESS_FIRMWARE_RESOURCE_ID, "1.0.0");
	if (rc != 0) {
		return rc;
	}

	rc = sirena_lwm2m_set_execute_handler(harness_execute_handler, NULL);
	if (rc != 0) {
		printk("sirena lwm2m conformance harness: the execute handler failed: %d\n", rc);
		return rc;
	}

	rc = sirena_lwm2m_transport_set_endpoint_name(CONFIG_SIRENA_LWM2M_CONFORMANCE_ENDPOINT_NAME);
	if (rc != 0) {
		printk("sirena lwm2m conformance harness: the endpoint name failed: %d\n", rc);
		return rc;
	}

	return 0;
}

static int harness_register(void)
{
	int64_t deadline = k_uptime_get() + CONFIG_SIRENA_LWM2M_CONFORMANCE_REGISTRATION_TIMEOUT_MS;
	int rc;

	do {
		rc = sirena_lwm2m_transport_register();
		if (rc == 0) {
			return 0;
		}

		printk("sirena lwm2m conformance harness: the registration failed: %d\n", rc);
		k_sleep(K_MSEC(HARNESS_RETRY_INTERVAL_MS));
	} while (k_uptime_get() < deadline);

	return rc;
}

static int harness_publish_telemetry(void)
{
	struct sirena_lwm2m_value value = {
		.type = SIRENA_LWM2M_VALUE_INTEGER,
	};
	int rc;

	value.data.integer = CONFIG_SIRENA_LWM2M_CONFORMANCE_TELEMETRY_VALUE;

	rc = sirena_lwm2m_set_value(HARNESS_OBJECT_ID, HARNESS_INSTANCE_ID,
				    HARNESS_BATTERY_RESOURCE_ID, &value);
	if (rc != 0) {
		printk("sirena lwm2m conformance harness: the telemetry value failed: %d\n", rc);
		return rc;
	}

	rc = sirena_lwm2m_notify_resource(HARNESS_OBJECT_ID, HARNESS_INSTANCE_ID,
					  HARNESS_BATTERY_RESOURCE_ID);
	if (rc < 0) {
		printk("sirena lwm2m conformance harness: the notification failed: %d\n", rc);
		return rc;
	}

	k_mutex_lock(&harness_lock, K_FOREVER);
	harness_notifications += (uint32_t)rc;
	k_mutex_unlock(&harness_lock);

	printk("sirena lwm2m conformance harness: the telemetry %lld was sent to %d observers\n",
	       (long long)value.data.integer, rc);

	return 0;
}

static bool harness_write_applied(void)
{
	char value[HARNESS_UTC_OFFSET_MAX];
	size_t length = sizeof(value);

	if (sirena_lwm2m_get_string(HARNESS_OBJECT_ID, HARNESS_INSTANCE_ID,
				     HARNESS_UTC_OFFSET_RESOURCE_ID, value, &length) != 0) {
		return false;
	}

	return strcmp(value, CONFIG_SIRENA_LWM2M_CONFORMANCE_WRITE_VALUE) == 0;
}

ZTEST_SUITE(sirena_lwm2m_conformance, NULL, NULL, NULL, NULL, NULL);

ZTEST(sirena_lwm2m_conformance, test_the_client_registers_publishes_telemetry_and_takes_a_command)
{
	uint16_t object_id = 0U;
	uint16_t instance_id = 0U;
	uint16_t resource_id = 0U;
	int64_t deadline;
	int rc;

	rc = harness_prepare();
	zassert_equal(rc, 0, "the harness preparation failed: %d", rc);

	rc = sirena_lwm2m_transport_start();
	zassert_equal(rc, 0, "the transport failed: %d", rc);

	printk("sirena lwm2m conformance harness: registering with %s as %s\n",
	       CONFIG_SIRENA_LWM2M_CONFORMANCE_SERVER_URI, CONFIG_SIRENA_LWM2M_CONFORMANCE_ENDPOINT_NAME);

	rc = harness_register();
	zassert_equal(rc, 0, "the registration with the server failed: %d", rc);

	printk("sirena lwm2m conformance harness: registered\n");

	deadline = k_uptime_get() + CONFIG_SIRENA_LWM2M_CONFORMANCE_COMMAND_TIMEOUT_MS;

	while (k_uptime_get() < deadline) {
		(void)harness_publish_telemetry();

		if (harness_notification_count() > 0U && harness_write_applied() &&
		    harness_command_seen(&object_id, &instance_id, &resource_id)) {
			break;
		}

		k_sleep(K_MSEC(CONFIG_SIRENA_LWM2M_CONFORMANCE_TELEMETRY_INTERVAL_MS));
	}

	zassert_true(harness_notification_count() > 0U,
		     "the server did not receive a telemetry notification");
	zassert_true(harness_write_applied(),
		     "the server did not write the UTC offset resource of the client");
	zassert_true(harness_command_seen(&object_id, &instance_id, &resource_id),
		     "the server did not execute a command on the client");
	zassert_equal(object_id, HARNESS_OBJECT_ID, "the command carries an unexpected object");
	zassert_equal(instance_id, HARNESS_INSTANCE_ID, "the command carries an unexpected instance");
	zassert_equal(resource_id, HARNESS_REBOOT_RESOURCE_ID,
		      "the command carries an unexpected resource");

	rc = sirena_lwm2m_transport_deregister();
	zassert_equal(rc, 0, "the deregistration failed: %d", rc);

	printk("sirena lwm2m conformance harness: deregistered\n");
}
