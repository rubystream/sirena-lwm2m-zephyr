#include <zephyr/ztest.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/coap_client.h>
#include <zephyr/net/coap_service.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>
#include <sirena/lwm2m/codec.h>
#include <sirena/lwm2m/objects.h>
#include "sample_flow.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define PEER_LIFETIME 300U
#define PEER_LIFETIME_UPDATED 600U
#define PEER_ENDPOINT_NAME "sirena-sample-test"
#define PEER_LOCATION "rd"
#define PEER_LOCATION_ID "1"
#define PEER_PAYLOAD_MAX 256
#define PEER_OPTION_MAX 8
#define PEER_QUERY_MAX 8
#define PEER_QUERY_LEN 72

#define PEER_SERVER_OBJECT_ID 1U
#define PEER_SERVER_LIFETIME_RESOURCE 1U
#define PEER_DEVICE_OBJECT_ID 3U
#define PEER_DEVICE_MANUFACTURER_RESOURCE 0U
#define PEER_DEVICE_BATTERY_RESOURCE 9U
#define PEER_DEVICE_REBOOT_RESOURCE 4U
#define PEER_INSTANCE_ID 0U

#define SAMPLE_RUN_SECONDS 8U
#define SAMPLE_TELEMETRY_INTERVAL_MS 500U
#define SAMPLE_STACK_SIZE 4096
#define SAMPLE_BATTERY_START 100
#define SAMPLE_BATTERY_STEP 5

struct peer_capture {
	int count;
	int code;
	int content_format;
	int observe;
	int type;
	uint8_t token[COAP_TOKEN_MAX_LEN];
	uint8_t token_length;
	uint8_t payload[PEER_PAYLOAD_MAX];
	size_t payload_length;
};

static struct peer_capture peer_capture;
static struct net_sockaddr_storage peer_client_address;
static net_socklen_t peer_client_address_length;
static bool peer_registered;
static bool peer_deregistered;
static char peer_registration_payload[PEER_PAYLOAD_MAX];
static char peer_registration_query[PEER_QUERY_MAX][PEER_QUERY_LEN];
static uint8_t peer_registration_query_count;
static uint16_t peer_port;
static char server_uri[64];
static struct coap_client peer_client;
static int peer_socket = -1;
static struct sirena_lwm2m_sample_config sample_config;
static int sample_run_result;
static K_SEM_DEFINE(peer_semaphore, 0, 64);
static K_SEM_DEFINE(peer_registration_semaphore, 0, 4);
static K_THREAD_STACK_DEFINE(sample_stack, SAMPLE_STACK_SIZE);
static struct k_thread sample_thread;

static int peer_reply(struct coap_resource *resource, struct coap_packet *request,
		       struct net_sockaddr *addr, net_socklen_t addr_len, uint8_t code,
		       const char *const *location)
{
	uint8_t buffer[64];
	uint8_t token[COAP_TOKEN_MAX_LEN];
	uint8_t token_length = coap_header_get_token(request, token);
	uint8_t type = coap_header_get_type(request) == COAP_TYPE_CON ? COAP_TYPE_ACK
								    : COAP_TYPE_NON_CON;
	struct coap_packet response;
	int rc;
	int i;

	rc = coap_packet_init(&response, buffer, sizeof(buffer), COAP_VERSION_1, type, token_length,
			       token, code, coap_header_get_id(request));
	if (rc < 0) {
		return rc;
	}

	for (i = 0; location != NULL && location[i] != NULL; ++i) {
		rc = coap_packet_append_option(&response, COAP_OPTION_LOCATION_PATH,
					       (const uint8_t *)location[i],
					       (uint16_t)strlen(location[i]));
		if (rc < 0) {
			return rc;
		}
	}

	return coap_resource_send(resource, &response, addr, addr_len, NULL);
}

static int peer_register_post(struct coap_resource *resource, struct coap_packet *request,
			      struct net_sockaddr *addr, net_socklen_t addr_len)
{
	static const char *const location[] = { PEER_LOCATION, PEER_LOCATION_ID, NULL };
	struct coap_option options[PEER_OPTION_MAX];
	const uint8_t *payload;
	uint16_t payload_length;
	int count;
	int i;

	memset(peer_registration_payload, 0, sizeof(peer_registration_payload));
	peer_registration_query_count = 0U;

	count = coap_find_options(request, COAP_OPTION_URI_QUERY, options, ARRAY_SIZE(options));
	for (i = 0; i < count && peer_registration_query_count < PEER_QUERY_MAX; ++i) {
		size_t length = options[i].len;

		if (length >= PEER_QUERY_LEN) {
			continue;
		}

		memcpy(peer_registration_query[peer_registration_query_count], options[i].value,
		       length);
		peer_registration_query[peer_registration_query_count][length] = '\0';
		peer_registration_query_count++;
	}

	payload = coap_packet_get_payload(request, &payload_length);
	if (payload != NULL && payload_length < sizeof(peer_registration_payload)) {
		memcpy(peer_registration_payload, payload, payload_length);
		peer_registration_payload[payload_length] = '\0';
	}

	peer_registered = true;
	memcpy(&peer_client_address, addr, sizeof(peer_client_address));
	peer_client_address_length = addr_len;

	k_sem_give(&peer_registration_semaphore);

	return peer_reply(resource, request, addr, addr_len, COAP_RESPONSE_CODE_CREATED, location);
}

static int peer_update_post(struct coap_resource *resource, struct coap_packet *request,
			     struct net_sockaddr *addr, net_socklen_t addr_len)
{
	return peer_reply(resource, request, addr, addr_len, COAP_RESPONSE_CODE_CHANGED, NULL);
}

static int peer_delete(struct coap_resource *resource, struct coap_packet *request,
			struct net_sockaddr *addr, net_socklen_t addr_len)
{
	peer_deregistered = true;

	return peer_reply(resource, request, addr, addr_len, COAP_RESPONSE_CODE_DELETED, NULL);
}

COAP_SERVICE_DEFINE(peer_service, "127.0.0.1", &peer_port, 0);

static const char *const peer_register_path[] = { PEER_LOCATION, NULL };
COAP_RESOURCE_DEFINE(peer_register_resource, peer_service, {
	.path = peer_register_path,
	.post = peer_register_post,
});

static const char *const peer_location_path[] = { PEER_LOCATION, PEER_LOCATION_ID, NULL };
COAP_RESOURCE_DEFINE(peer_location_resource, peer_service, {
	.path = peer_location_path,
	.post = peer_update_post,
	.del = peer_delete,
});

static bool peer_has_query(const char *value)
{
	uint8_t i;

	for (i = 0U; i < peer_registration_query_count; ++i) {
		if (strcmp(peer_registration_query[i], value) == 0) {
			return true;
		}
	}

	return false;
}

static void peer_response_handler(const struct coap_client_response_data *data, void *user_data)
{
	(void)user_data;

	peer_capture.count++;
	peer_capture.code = data->result_code;
	peer_capture.content_format = -1;
	peer_capture.observe = -1;
	peer_capture.type = -1;
	peer_capture.token_length = 0U;
	peer_capture.payload_length = 0U;

	if (data->packet != NULL) {
		peer_capture.content_format = coap_get_option_int(data->packet,
								  COAP_OPTION_CONTENT_FORMAT);
		peer_capture.observe = coap_get_option_int(data->packet, COAP_OPTION_OBSERVE);
		peer_capture.token_length = coap_header_get_token(data->packet, peer_capture.token);
		peer_capture.type = coap_header_get_type(data->packet);
	}

	if (data->payload != NULL && data->payload_len < sizeof(peer_capture.payload)) {
		memcpy(peer_capture.payload, data->payload, data->payload_len);
		peer_capture.payload_length = data->payload_len;
	}

	k_sem_give(&peer_semaphore);
}

static int peer_request(enum coap_method method, const char *path, uint16_t content_format,
			 const void *payload, size_t payload_length, int observe)
{
	struct coap_client_request req = {0};
	int rc;

	req.method = method;
	req.confirmable = true;
	req.fmt = (enum coap_content_format)content_format;
	req.payload = payload;
	req.len = payload_length;
	req.cb = peer_response_handler;
	req.user_data = NULL;

	if (strlen(path) >= sizeof(req.path)) {
		return -EINVAL;
	}

	strcpy(req.path, path);

	if (observe >= 0) {
		req.options[req.num_options].code = COAP_OPTION_OBSERVE;
		req.options[req.num_options].len = 1U;
		req.options[req.num_options].value[0] = (uint8_t)observe;
		req.num_options++;
	}

	rc = coap_client_req(&peer_client, peer_socket, net_sad(&peer_client_address), &req, NULL);
	if (rc != 0) {
		return rc;
	}

	rc = k_sem_take(&peer_semaphore, K_SECONDS(15));
	if (rc != 0) {
		return -ETIMEDOUT;
	}

	return 0;
}

static size_t peer_encode_integer(uint16_t object_id, uint16_t resource_id, int64_t value,
				   uint8_t *buffer, size_t capacity)
{
	struct sirena_lwm2m_codec_value codec_value = {
		.object_id = object_id,
		.instance_id = PEER_INSTANCE_ID,
		.resource_id = resource_id,
		.id = resource_id,
		.type = SIRENA_LWM2M_VALUE_INTEGER,
	};
	size_t length = capacity;
	int rc;

	codec_value.data.integer = value;

	rc = sirena_lwm2m_codec_encode(SIRENA_LWM2M_CF_TLV, &codec_value, 1U, buffer, &length);
	zassert_equal(rc, 0, "the tlv encoding failed: %d", rc);

	return length;
}

static bool peer_payload_contains(const char *needle)
{
	size_t needle_length = strlen(needle);
	size_t i;

	if (peer_capture.payload_length < needle_length) {
		return false;
	}

	for (i = 0U; i + needle_length <= peer_capture.payload_length; ++i) {
		if (memcmp(&peer_capture.payload[i], needle, needle_length) == 0) {
			return true;
		}
	}

	return false;
}

static int64_t peer_decode_integer(void)
{
	struct sirena_lwm2m_codec_value values[4];
	size_t count = ARRAY_SIZE(values);
	int rc;

	rc = sirena_lwm2m_codec_decode(SIRENA_LWM2M_CF_TLV, peer_capture.payload,
					peer_capture.payload_length, values, &count);
	zassert_equal(rc, 0, "the tlv decoding failed: %d", rc);
	zassert_equal(count, 1U, "unexpected value count");
	zassert_equal(values[0].type, SIRENA_LWM2M_VALUE_INTEGER, "unexpected value type");

	return values[0].data.integer;
}

static void sample_thread_entry(void *first, void *second, void *third)
{
	(void)first;
	(void)second;
	(void)third;

	sample_run_result = sirena_lwm2m_sample_run(&sample_config);
}

static void *suite_setup(void)
{
	struct net_sockaddr_in address;
	net_socklen_t length = sizeof(address);
	int rc;

	rc = coap_service_start(&peer_service);
	zassert_equal(rc, 0, "peer service start failed: %d", rc);

	memset(&address, 0, sizeof(address));
	address.sin_family = NET_AF_INET;
	rc = zsock_getsockname(peer_service.data->sock_fd, (struct net_sockaddr *)&address, &length);
	zassert_equal(rc, 0, "peer port lookup failed: %d", rc);

	rc = snprintf(server_uri, sizeof(server_uri), "coap://127.0.0.1:%u",
		      net_ntohs(address.sin_port));
	zassert_true(rc > 0 && rc < (int)sizeof(server_uri), "server uri build failed");

	peer_socket = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
	zassert_true(peer_socket >= 0, "peer socket creation failed");

	rc = coap_client_init(&peer_client, "sirena_lwm2m_sample_peer");
	zassert_equal(rc, 0, "peer client init failed: %d", rc);

	memset(&sample_config, 0, sizeof(sample_config));
	sample_config.server_uri = server_uri;
	sample_config.endpoint_name = PEER_ENDPOINT_NAME;
	sample_config.security_mode = SIRENA_LWM2M_SECURITY_MODE_NOSEC;
	sample_config.short_server_id = 1U;
	sample_config.lifetime = PEER_LIFETIME;
	sample_config.binding = "U";
	sample_config.manufacturer = "Sirena";
	sample_config.model_number = "sirena-sample";
	sample_config.serial_number = "sirena-sample-0001";
	sample_config.firmware_version = "1.0.0";
	sample_config.run_seconds = SAMPLE_RUN_SECONDS;
	sample_config.telemetry_interval_ms = SAMPLE_TELEMETRY_INTERVAL_MS;

	return NULL;
}

static void test_before(void *fixture)
{
	(void)fixture;

	memset(&peer_capture, 0, sizeof(peer_capture));
	memset(peer_registration_payload, 0, sizeof(peer_registration_payload));
	peer_registration_query_count = 0U;
	peer_registered = false;
	peer_deregistered = false;
	k_sem_reset(&peer_semaphore);
	k_sem_reset(&peer_registration_semaphore);
}

ZTEST_SUITE(sirena_lwm2m_sample, NULL, suite_setup, test_before, NULL, NULL);

ZTEST(sirena_lwm2m_sample, test_the_sample_registers_publishes_telemetry_and_handles_a_command)
{
	const struct sirena_lwm2m_sample_state *state;
	uint8_t payload[PEER_PAYLOAD_MAX];
	int64_t observed;
	int64_t notified;
	size_t payload_length;
	int rc;

	k_thread_create(&sample_thread, sample_stack, SAMPLE_STACK_SIZE, sample_thread_entry, NULL,
			NULL, NULL, 5, 0, K_NO_WAIT);

	rc = k_sem_take(&peer_registration_semaphore, K_SECONDS(15));
	zassert_equal(rc, 0, "the peer received no registration");

	zassert_true(peer_registered, "the peer received no registration");
	zassert_not_null(strstr(peer_registration_payload, "</1/0>"),
			 "the registration payload must carry the server object");
	zassert_not_null(strstr(peer_registration_payload, "</3/0>"),
			 "the registration payload must carry the device object");
	zassert_true(strstr(peer_registration_payload, "</0/0>") == NULL,
		     "the registration payload must not carry the security object");
	zassert_true(peer_has_query("ep=sirena-sample-test"),
		     "the registration must carry the endpoint name");
	zassert_true(peer_has_query("lwm2m=1.2"), "the registration must carry the lwm2m version");
	zassert_true(peer_has_query("lt=300"), "the registration must carry the lifetime");

	rc = peer_request(COAP_METHOD_GET, "/3/0/9", 0U, NULL, 0U, 0);
	zassert_equal(rc, 0, "the observe request failed: %d", rc);
	zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_CONTENT, "unexpected response code");
	zassert_true(peer_capture.observe >= 0, "the response must carry the observe option");

	observed = peer_decode_integer();
	zassert_true(observed >= 0 && observed <= SAMPLE_BATTERY_START,
		     "the observed value is out of range: %lld", (long long)observed);

	rc = k_sem_take(&peer_semaphore, K_SECONDS(15));
	zassert_equal(rc, 0, "no notification was received");

	zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_CONTENT, "unexpected notification code");
	zassert_equal(peer_capture.type, COAP_TYPE_CON,
		      "the notification must be a confirmable message");

	notified = peer_decode_integer();
	zassert_true(notified >= 0 && notified <= SAMPLE_BATTERY_START,
		     "the notified value is out of range: %lld", (long long)notified);
	zassert_equal(notified % SAMPLE_BATTERY_STEP, 0,
		      "the notified value is not one the sample publishes");

	state = sirena_lwm2m_sample_state();
	zassert_true(state->telemetry_reports > 0, "the sample published no telemetry");
	zassert_true(state->last_telemetry_value <= notified,
		     "the sample published a value the peer did not receive");

	rc = peer_request(COAP_METHOD_POST, "/3/0/4", 0U, NULL, 0U, -1);
	zassert_equal(rc, 0, "the execute request failed: %d", rc);
	zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_CHANGED, "unexpected response code");

	state = sirena_lwm2m_sample_state();
	zassert_true(state->command_handled, "the execute handler of the sample did not run");
	zassert_equal(state->command_object_id, PEER_DEVICE_OBJECT_ID, "unexpected object id");
	zassert_equal(state->command_instance_id, PEER_INSTANCE_ID, "unexpected instance id");
	zassert_equal(state->command_resource_id, PEER_DEVICE_REBOOT_RESOURCE,
		      "unexpected resource id");

	rc = peer_request(COAP_METHOD_GET, "/3/0/0", 0U, NULL, 0U, -1);
	zassert_equal(rc, 0, "the read request failed: %d", rc);
	zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_CONTENT, "unexpected response code");
	zassert_true(peer_payload_contains("Sirena"),
		     "the read must carry the manufacturer of the sample");

	payload_length = peer_encode_integer(PEER_SERVER_OBJECT_ID, PEER_SERVER_LIFETIME_RESOURCE,
					     PEER_LIFETIME_UPDATED, payload, sizeof(payload));

	rc = peer_request(COAP_METHOD_PUT, "/1/0/1", SIRENA_LWM2M_CF_TLV, payload, payload_length,
			  -1);
	zassert_equal(rc, 0, "the write request failed: %d", rc);
	zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_CHANGED, "unexpected response code");

	rc = peer_request(COAP_METHOD_GET, "/1/0/1", 0U, NULL, 0U, -1);
	zassert_equal(rc, 0, "the read request failed: %d", rc);
	zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_CONTENT, "unexpected response code");
	zassert_equal(peer_decode_integer(), PEER_LIFETIME_UPDATED,
		      "the written value was not stored");

	rc = k_thread_join(&sample_thread, K_SECONDS(SAMPLE_RUN_SECONDS + 15));
	zassert_equal(rc, 0, "the sample did not stop on its own");
	zassert_equal(sample_run_result, 0, "the sample run failed: %d", sample_run_result);

	zassert_true(peer_deregistered, "the peer received no deregistration");

	state = sirena_lwm2m_sample_state();
	zassert_true(state->registered, "the sample was not registered");
	zassert_equal(state->last_error, 0, "the sample reported an error");
}
