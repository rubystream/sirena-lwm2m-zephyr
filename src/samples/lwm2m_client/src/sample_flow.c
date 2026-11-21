#include "sample_flow.h"
#include <sirena/lwm2m/object_registry.h>
#include <sirena/lwm2m/objects.h>
#include <sirena/lwm2m/protocol.h>
#include <sirena/lwm2m/transport.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <errno.h>
#include <string.h>

#define SIRENA_LWM2M_SAMPLE_BATTERY_START 100
#define SIRENA_LWM2M_SAMPLE_BATTERY_STEP 5

static K_MUTEX_DEFINE(sample_lock);
static struct sirena_lwm2m_sample_state sample_state;

static void sample_execute_handler(uint16_t object_id, uint16_t instance_id,
                                   uint16_t resource_id, void *user_data)
{
	(void)user_data;

	k_mutex_lock(&sample_lock, K_FOREVER);
	sample_state.command_handled = true;
	sample_state.command_object_id = object_id;
	sample_state.command_instance_id = instance_id;
	sample_state.command_resource_id = resource_id;
	k_mutex_unlock(&sample_lock);

	printk("sirena lwm2m sample: command %u/%u/%u received\n", object_id, instance_id,
	       resource_id);
}

static int sample_set_device_string(uint16_t resource_id, const char *value)
{
	if (value == NULL) {
		return 0;
	}

	return sirena_lwm2m_set_string(SIRENA_LWM2M_SAMPLE_DEVICE_OBJECT_ID,
				       SIRENA_LWM2M_SAMPLE_INSTANCE_ID, resource_id, value);
}

static int sample_prepare(const struct sirena_lwm2m_sample_config *config)
{
	struct sirena_lwm2m_provisioning provisioning = {
		.server_uri = config->server_uri,
		.security_mode = config->security_mode,
		.identity = config->identity,
		.identity_length = config->identity_length,
		.secret_key = config->secret_key,
		.secret_key_length = config->secret_key_length,
		.short_server_id = config->short_server_id,
		.lifetime = config->lifetime,
		.binding = config->binding,
	};
	struct sirena_lwm2m_resource *resource;
	int rc;

	rc = sirena_lwm2m_registry_init();
	if (rc != 0) {
		printk("sirena lwm2m sample: the registry failed: %d\n", rc);
		return rc;
	}

	rc = sirena_lwm2m_objects_init();
	if (rc != 0) {
		printk("sirena lwm2m sample: the objects failed: %d\n", rc);
		return rc;
	}

	rc = sirena_lwm2m_provision(&provisioning);
	if (rc != 0) {
		printk("sirena lwm2m sample: the provisioning failed: %d\n", rc);
		return rc;
	}

	rc = sample_set_device_string(SIRENA_LWM2M_SAMPLE_MANUFACTURER_RESOURCE_ID,
				       config->manufacturer);
	if (rc != 0) {
		printk("sirena lwm2m sample: the manufacturer failed: %d\n", rc);
		return rc;
	}

	rc = sample_set_device_string(SIRENA_LWM2M_SAMPLE_MODEL_RESOURCE_ID,
				       config->model_number);
	if (rc != 0) {
		printk("sirena lwm2m sample: the model number failed: %d\n", rc);
		return rc;
	}

	rc = sample_set_device_string(SIRENA_LWM2M_SAMPLE_SERIAL_RESOURCE_ID,
				       config->serial_number);
	if (rc != 0) {
		printk("sirena lwm2m sample: the serial number failed: %d\n", rc);
		return rc;
	}

	rc = sample_set_device_string(SIRENA_LWM2M_SAMPLE_FIRMWARE_RESOURCE_ID,
				       config->firmware_version);
	if (rc != 0) {
		printk("sirena lwm2m sample: the firmware version failed: %d\n", rc);
		return rc;
	}

	rc = sirena_lwm2m_find_resource(SIRENA_LWM2M_SAMPLE_DEVICE_OBJECT_ID,
					SIRENA_LWM2M_SAMPLE_INSTANCE_ID,
					SIRENA_LWM2M_SAMPLE_BATTERY_RESOURCE_ID, &resource);
	if (rc != 0) {
		printk("sirena lwm2m sample: the telemetry resource %u is not registered\n",
		       SIRENA_LWM2M_SAMPLE_BATTERY_RESOURCE_ID);
		return -ENOTSUP;
	}

	rc = sirena_lwm2m_set_execute_handler(sample_execute_handler, NULL);
	if (rc != 0) {
		printk("sirena lwm2m sample: the execute handler failed: %d\n", rc);
		return rc;
	}

	rc = sirena_lwm2m_transport_set_endpoint_name(config->endpoint_name);
	if (rc != 0) {
		printk("sirena lwm2m sample: the endpoint name failed: %d\n", rc);
		return rc;
	}

	return 0;
}

static void sample_run_telemetry(const struct sirena_lwm2m_sample_config *config)
{
	int64_t deadline = k_uptime_get() + (int64_t)config->run_seconds * 1000;
	int64_t value = SIRENA_LWM2M_SAMPLE_BATTERY_START;

	while (k_uptime_get() < deadline) {
		int rc;

		k_sleep(K_MSEC(config->telemetry_interval_ms));

		if (value > 0) {
			value -= SIRENA_LWM2M_SAMPLE_BATTERY_STEP;
		}

		rc = sirena_lwm2m_set_integer(SIRENA_LWM2M_SAMPLE_DEVICE_OBJECT_ID,
					      SIRENA_LWM2M_SAMPLE_INSTANCE_ID,
					      SIRENA_LWM2M_SAMPLE_BATTERY_RESOURCE_ID, value);
		if (rc != 0) {
			printk("sirena lwm2m sample: the telemetry value failed: %d\n", rc);
			continue;
		}

		rc = sirena_lwm2m_notify_resource(SIRENA_LWM2M_SAMPLE_DEVICE_OBJECT_ID,
						  SIRENA_LWM2M_SAMPLE_INSTANCE_ID,
						  SIRENA_LWM2M_SAMPLE_BATTERY_RESOURCE_ID);
		if (rc < 0) {
			printk("sirena lwm2m sample: the notification failed: %d\n", rc);
			continue;
		}

		k_mutex_lock(&sample_lock, K_FOREVER);
		sample_state.telemetry_reports++;
		sample_state.notifications_sent += (uint32_t)rc;
		sample_state.last_telemetry_value = value;
		k_mutex_unlock(&sample_lock);

		printk("sirena lwm2m sample: telemetry %lld published to %d observers\n",
		       (long long)value, rc);
	}
}

void sirena_lwm2m_sample_config_from_kconfig(struct sirena_lwm2m_sample_config *config)
{
	if (config == NULL) {
		return;
	}

	memset(config, 0, sizeof(*config));

	config->server_uri = CONFIG_SIRENA_LWM2M_SAMPLE_SERVER_URI;
	config->endpoint_name = CONFIG_SIRENA_LWM2M_SAMPLE_ENDPOINT_NAME;
	config->security_mode = (uint8_t)CONFIG_SIRENA_LWM2M_SAMPLE_SECURITY_MODE;
	config->identity = CONFIG_SIRENA_LWM2M_SAMPLE_IDENTITY;
	config->identity_length = sizeof(CONFIG_SIRENA_LWM2M_SAMPLE_IDENTITY) - 1U;
	config->secret_key = CONFIG_SIRENA_LWM2M_SAMPLE_SECRET_KEY;
	config->secret_key_length = sizeof(CONFIG_SIRENA_LWM2M_SAMPLE_SECRET_KEY) - 1U;
	config->short_server_id = (uint16_t)CONFIG_SIRENA_LWM2M_SAMPLE_SHORT_SERVER_ID;
	config->lifetime = (uint32_t)CONFIG_SIRENA_LWM2M_SAMPLE_LIFETIME;
	config->binding = CONFIG_SIRENA_LWM2M_SAMPLE_BINDING;
	config->manufacturer = CONFIG_SIRENA_LWM2M_SAMPLE_MANUFACTURER;
	config->model_number = CONFIG_SIRENA_LWM2M_SAMPLE_MODEL_NUMBER;
	config->serial_number = CONFIG_SIRENA_LWM2M_SAMPLE_SERIAL_NUMBER;
	config->firmware_version = CONFIG_SIRENA_LWM2M_SAMPLE_FIRMWARE_VERSION;
	config->run_seconds = (uint32_t)CONFIG_SIRENA_LWM2M_SAMPLE_RUN_SECONDS;
	config->telemetry_interval_ms = (uint32_t)CONFIG_SIRENA_LWM2M_SAMPLE_TELEMETRY_INTERVAL_MS;
}

const struct sirena_lwm2m_sample_state *sirena_lwm2m_sample_state(void)
{
	return &sample_state;
}

int sirena_lwm2m_sample_run(const struct sirena_lwm2m_sample_config *config)
{
	int rc;

	if (config == NULL) {
		return -EINVAL;
	}

	memset(&sample_state, 0, sizeof(sample_state));

	rc = sample_prepare(config);
	if (rc != 0) {
		sample_state.last_error = rc;
		return rc;
	}

	printk("sirena lwm2m sample: registering with %s as %s\n", config->server_uri,
	       config->endpoint_name);

	rc = sirena_lwm2m_transport_start();
	if (rc != 0) {
		printk("sirena lwm2m sample: the transport failed: %d\n", rc);
		sample_state.last_error = rc;
		return rc;
	}

	rc = sirena_lwm2m_transport_register();
	if (rc != 0) {
		printk("sirena lwm2m sample: the registration failed: %d\n", rc);
		sample_state.last_error = rc;
	} else {
		sample_state.registered = true;
		printk("sirena lwm2m sample: registered\n");
	}

	sample_run_telemetry(config);

	rc = sirena_lwm2m_transport_deregister();
	if (rc != 0) {
		printk("sirena lwm2m sample: the deregistration failed: %d\n", rc);
		sample_state.last_error = rc;
	} else {
		printk("sirena lwm2m sample: deregistered\n");
	}

	return 0;
}
