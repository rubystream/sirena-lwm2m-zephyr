#ifndef SIRENA_LWM2M_SAMPLE_FLOW_H
#define SIRENA_LWM2M_SAMPLE_FLOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SIRENA_LWM2M_SAMPLE_DEVICE_OBJECT_ID 3U
#define SIRENA_LWM2M_SAMPLE_INSTANCE_ID 0U
#define SIRENA_LWM2M_SAMPLE_MANUFACTURER_RESOURCE_ID 0U
#define SIRENA_LWM2M_SAMPLE_MODEL_RESOURCE_ID 1U
#define SIRENA_LWM2M_SAMPLE_SERIAL_RESOURCE_ID 2U
#define SIRENA_LWM2M_SAMPLE_FIRMWARE_RESOURCE_ID 3U
#define SIRENA_LWM2M_SAMPLE_BATTERY_RESOURCE_ID 9U
#define SIRENA_LWM2M_SAMPLE_REBOOT_RESOURCE_ID 4U

struct sirena_lwm2m_sample_config {
	const char *server_uri;
	const char *endpoint_name;
	uint8_t security_mode;
	const void *identity;
	size_t identity_length;
	const void *secret_key;
	size_t secret_key_length;
	uint16_t short_server_id;
	uint32_t lifetime;
	const char *binding;
	const char *manufacturer;
	const char *model_number;
	const char *serial_number;
	const char *firmware_version;
	uint32_t run_seconds;
	uint32_t telemetry_interval_ms;
};

struct sirena_lwm2m_sample_state {
	bool registered;
	bool command_handled;
	uint16_t command_object_id;
	uint16_t command_instance_id;
	uint16_t command_resource_id;
	uint32_t telemetry_reports;
	uint32_t notifications_sent;
	int64_t last_telemetry_value;
	int last_error;
};

void sirena_lwm2m_sample_config_from_kconfig(struct sirena_lwm2m_sample_config *config);
int sirena_lwm2m_sample_run(const struct sirena_lwm2m_sample_config *config);
const struct sirena_lwm2m_sample_state *sirena_lwm2m_sample_state(void);

#endif
