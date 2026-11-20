#include <zephyr/ztest.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/coap_service.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>
#include <sirena/lwm2m/coap.h>
#include <sirena/lwm2m/object_registry.h>
#include <sirena/lwm2m/objects.h>
#include <sirena/lwm2m/transport.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define PEER_LIFETIME 300U
#define PEER_LOCATION "rd"
#define PEER_LOCATION_ID "1"
#define PEER_OPTION_MAX 8
#define PEER_QUERY_MAX 8
#define PEER_QUERY_LEN 72
#define PEER_ENDPOINT_NAME "sirena-test"
#define PEER_ENDPOINT_NAME_MAX "sirena-endpoint-name-max-32chars"

struct peer_request {
	bool received;
	uint8_t method;
	char path[64];
	uint8_t query_count;
	char query[PEER_QUERY_MAX][PEER_QUERY_LEN];
	int content_format;
	char payload[320];
};

static struct peer_request peer;
static bool peer_reject_register;
static uint16_t peer_port;
static char server_uri[64];
static char minimal_server_uri[64];
static int64_t minimal_lifetime = PEER_LIFETIME;

static void peer_append_path(const uint8_t *value, uint16_t length)
{
	size_t used = strlen(peer.path);

	if (used + length + 2U > sizeof(peer.path)) {
		return;
	}

	peer.path[used] = '/';
	memcpy(&peer.path[used + 1U], value, length);
	peer.path[used + 1U + length] = '\0';
}

static void peer_record(struct coap_packet *request, uint8_t method)
{
	struct coap_option options[PEER_OPTION_MAX];
	const uint8_t *payload;
	uint16_t payload_length;
	int count;
	int i;

	memset(&peer, 0, sizeof(peer));
	peer.received = true;
	peer.method = method;
	peer.content_format = coap_get_option_int(request, COAP_OPTION_CONTENT_FORMAT);

	count = coap_find_options(request, COAP_OPTION_URI_PATH, options, ARRAY_SIZE(options));
	for (i = 0; i < count; ++i) {
		peer_append_path(options[i].value, options[i].len);
	}

	count = coap_find_options(request, COAP_OPTION_URI_QUERY, options, ARRAY_SIZE(options));
	for (i = 0; i < count && peer.query_count < PEER_QUERY_MAX; ++i) {
		size_t length = options[i].len;

		if (length >= PEER_QUERY_LEN) {
			continue;
		}

		memcpy(peer.query[peer.query_count], options[i].value, length);
		peer.query[peer.query_count][length] = '\0';
		peer.query_count++;
	}

	payload = coap_packet_get_payload(request, &payload_length);
	if (payload != NULL && payload_length < sizeof(peer.payload)) {
		memcpy(peer.payload, payload, payload_length);
		peer.payload[payload_length] = '\0';
	}
}

static bool peer_has_query(const char *value)
{
	uint8_t i;

	for (i = 0; i < peer.query_count; ++i) {
		if (strcmp(peer.query[i], value) == 0) {
			return true;
		}
	}

	return false;
}

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

	peer_record(request, COAP_METHOD_POST);

	if (peer_reject_register) {
		return peer_reply(resource, request, addr, addr_len,
				  COAP_RESPONSE_CODE_PRECONDITION_FAILED, location);
	}

	return peer_reply(resource, request, addr, addr_len, COAP_RESPONSE_CODE_CREATED, location);
}

static int peer_update_post(struct coap_resource *resource, struct coap_packet *request,
			     struct net_sockaddr *addr, net_socklen_t addr_len)
{
	peer_record(request, COAP_METHOD_POST);

	return peer_reply(resource, request, addr, addr_len, COAP_RESPONSE_CODE_CHANGED, NULL);
}

static int peer_delete(struct coap_resource *resource, struct coap_packet *request,
			struct net_sockaddr *addr, net_socklen_t addr_len)
{
	peer_record(request, COAP_METHOD_DELETE);

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

static void peer_provision(const char *uri)
{
	struct sirena_lwm2m_provisioning provisioning = {
		.server_uri = uri,
		.security_mode = SIRENA_LWM2M_SECURITY_MODE_NOSEC,
		.short_server_id = 1U,
		.lifetime = PEER_LIFETIME,
		.binding = "U",
	};
	int rc;

	rc = sirena_lwm2m_provision(&provisioning);
	zassert_equal(rc, 0, "provisioning failed");
}

static void peer_provision_psk(const char *uri)
{
	struct sirena_lwm2m_provisioning provisioning = {
		.server_uri = uri,
		.security_mode = SIRENA_LWM2M_SECURITY_MODE_PSK,
		.identity = "sirena-test-identity",
		.identity_length = sizeof("sirena-test-identity") - 1U,
		.secret_key = "0123456789abcdef",
		.secret_key_length = sizeof("0123456789abcdef") - 1U,
		.short_server_id = 1U,
		.lifetime = PEER_LIFETIME,
		.binding = "U",
	};
	int rc;

	rc = sirena_lwm2m_provision(&provisioning);
	zassert_equal(rc, 0, "provisioning failed");
}

static int minimal_read_string(uint16_t object_id, uint16_t instance_id, uint16_t resource_id,
			       void *user_data, void *buffer, size_t *length)
{
	size_t used = strlen(minimal_server_uri);

	(void)object_id;
	(void)instance_id;
	(void)resource_id;
	(void)user_data;

	if (buffer == NULL || (length != NULL && *length < used)) {
		return -ENOMEM;
	}

	memcpy(buffer, minimal_server_uri, used);
	if (length != NULL) {
		*length = used;
	}

	return 0;
}

static int minimal_read_integer(uint16_t object_id, uint16_t instance_id, uint16_t resource_id,
				void *user_data, void *buffer, size_t *length)
{
	(void)object_id;
	(void)instance_id;
	(void)resource_id;
	(void)user_data;

	if (buffer == NULL || length == NULL || *length < sizeof(minimal_lifetime)) {
		return -ENOMEM;
	}

	memcpy(buffer, &minimal_lifetime, sizeof(minimal_lifetime));
	*length = sizeof(minimal_lifetime);

	return 0;
}

static void peer_provision_without_device_object(void)
{
	int rc;

	rc = snprintf(minimal_server_uri, sizeof(minimal_server_uri), "%s", server_uri);
	zassert_true(rc > 0 && rc < (int)sizeof(minimal_server_uri), "server uri copy failed");

	rc = sirena_lwm2m_registry_init();
	zassert_equal(rc, 0, "registry init failed");

	rc = sirena_lwm2m_register_object(0U, false);
	zassert_equal(rc, 0, "security object registration failed");

	rc = sirena_lwm2m_create_instance(0U, 0U, NULL);
	zassert_equal(rc, 0, "security instance creation failed");

	rc = sirena_lwm2m_add_resource(0U, 0U, 0U, SIRENA_LWM2M_VALUE_STRING, SIRENA_LWM2M_OP_READ,
				       false, minimal_read_string, NULL, NULL, NULL);
	zassert_equal(rc, 0, "security resource registration failed");

	rc = sirena_lwm2m_register_object(1U, false);
	zassert_equal(rc, 0, "server object registration failed");

	rc = sirena_lwm2m_create_instance(1U, 0U, NULL);
	zassert_equal(rc, 0, "server instance creation failed");

	rc = sirena_lwm2m_add_resource(1U, 0U, 1U, SIRENA_LWM2M_VALUE_INTEGER, SIRENA_LWM2M_OP_READ,
				       false, minimal_read_integer, NULL, NULL, NULL);
	zassert_equal(rc, 0, "server resource registration failed");
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
	rc = zsock_getsockname(peer_service.data->sock_fd, (struct net_sockaddr *)&address,
			       &length);
	zassert_equal(rc, 0, "peer port lookup failed: %d", rc);

	rc = snprintf(server_uri, sizeof(server_uri), "coap://127.0.0.1:%u",
		      net_ntohs(address.sin_port));
	zassert_true(rc > 0 && rc < (int)sizeof(server_uri), "server uri build failed");

	return NULL;
}

static void test_before(void *fixture)
{
	int rc;

	(void)fixture;

	memset(&peer, 0, sizeof(peer));
	peer_reject_register = false;

	rc = sirena_lwm2m_registry_init();
	zassert_equal(rc, 0, "registry init failed");

	rc = sirena_lwm2m_objects_init();
	zassert_equal(rc, 0, "objects init failed");

	rc = sirena_lwm2m_coap_set_endpoint_name("");
	zassert_equal(rc, 0, "endpoint name reset failed");

	peer_provision(server_uri);

	rc = sirena_lwm2m_coap_start();
	zassert_equal(rc, 0, "transport start failed: %d", rc);
}

ZTEST_SUITE(sirena_lwm2m_coap, NULL, suite_setup, test_before, NULL, NULL);

ZTEST(sirena_lwm2m_coap, test_register_sends_expected_request)
{
	int rc;

	rc = sirena_lwm2m_coap_set_endpoint_name(PEER_ENDPOINT_NAME);
	zassert_equal(rc, 0, "endpoint name rejected");

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, 0, "register failed");

	zassert_true(peer.received, "peer received no request");
	zassert_equal(peer.method, COAP_METHOD_POST, "unexpected method");
	zassert_str_equal(peer.path, "/rd", "unexpected path");
	zassert_equal(peer.query_count, 4, "the query must carry one option per parameter");
	zassert_true(peer_has_query("ep=sirena-test"), "missing endpoint name");
	zassert_true(peer_has_query("lt=300"), "missing lifetime");
	zassert_true(peer_has_query("lwm2m=1.2"), "missing lwm2m version");
	zassert_true(peer_has_query("b=U"), "missing binding mode");
	zassert_equal(peer.content_format, COAP_CONTENT_FORMAT_APP_LINK_FORMAT,
		      "unexpected content format");
	zassert_str_equal(peer.payload, "</1/0>,</3/0>", "unexpected registration payload");
}

ZTEST(sirena_lwm2m_coap, test_register_carries_maximum_endpoint_name)
{
	int rc;

	rc = sirena_lwm2m_coap_set_endpoint_name(PEER_ENDPOINT_NAME_MAX);
	zassert_equal(rc, 0, "endpoint name rejected");

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, 0, "register failed");

	zassert_true(peer.received, "peer received no request");
	zassert_equal(peer.query_count, 4, "the query must carry one option per parameter");
	zassert_true(peer_has_query("ep=sirena-endpoint-name-max-32chars"),
		     "the endpoint name did not reach the peer");
}

ZTEST(sirena_lwm2m_coap, test_register_payload_follows_registry)
{
	int rc;

	rc = sirena_lwm2m_coap_set_endpoint_name(PEER_ENDPOINT_NAME);
	zassert_equal(rc, 0, "endpoint name rejected");

	rc = sirena_lwm2m_create_instance(3U, 1U, NULL);
	zassert_equal(rc, 0, "instance creation failed");

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, 0, "register failed");

	zassert_not_null(strstr(peer.payload, "</3/1>"), "missing extra instance");
	zassert_not_null(strstr(peer.payload, "</3/0>"), "missing device instance");
}

ZTEST(sirena_lwm2m_coap, test_register_requires_mandatory_objects)
{
	int rc;

	rc = sirena_lwm2m_coap_set_endpoint_name(PEER_ENDPOINT_NAME);
	zassert_equal(rc, 0, "endpoint name rejected");

	peer_provision_without_device_object();

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, -EINVAL, "a registration without the mandatory objects must fail");
	zassert_false(peer.received, "no request must be sent");
}

ZTEST(sirena_lwm2m_coap, test_register_stores_location_path)
{
	char location[64];
	size_t length = sizeof(location);
	int rc;

	rc = sirena_lwm2m_coap_set_endpoint_name(PEER_ENDPOINT_NAME);
	zassert_equal(rc, 0, "endpoint name rejected");

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, 0, "register failed");

	rc = sirena_lwm2m_coap_get_location_path(location, &length);
	zassert_equal(rc, 0, "location path unavailable");
	zassert_str_equal(location, "/rd/1", "unexpected location path");
}

ZTEST(sirena_lwm2m_coap, test_update_uses_location_path)
{
	int rc;

	rc = sirena_lwm2m_coap_set_endpoint_name(PEER_ENDPOINT_NAME);
	zassert_equal(rc, 0, "endpoint name rejected");

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, 0, "register failed");

	rc = sirena_lwm2m_transport_update();
	zassert_equal(rc, 0, "update failed");

	zassert_true(peer.received, "peer received no request");
	zassert_equal(peer.method, COAP_METHOD_POST, "unexpected method");
	zassert_str_equal(peer.path, "/rd/1", "unexpected path");
	zassert_equal(peer.query_count, 2, "the query must carry one option per parameter");
	zassert_true(peer_has_query("lt=300"), "missing lifetime");
	zassert_true(peer_has_query("b=U"), "missing binding mode");
	zassert_equal(peer.content_format, COAP_CONTENT_FORMAT_APP_LINK_FORMAT,
		      "the update must carry the link format");
}

ZTEST(sirena_lwm2m_coap, test_deregister_uses_location_path)
{
	int rc;

	rc = sirena_lwm2m_coap_set_endpoint_name(PEER_ENDPOINT_NAME);
	zassert_equal(rc, 0, "endpoint name rejected");

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, 0, "register failed");

	rc = sirena_lwm2m_transport_deregister();
	zassert_equal(rc, 0, "deregister failed");

	zassert_true(peer.received, "peer received no request");
	zassert_equal(peer.method, COAP_METHOD_DELETE, "unexpected method");
	zassert_str_equal(peer.path, "/rd/1", "unexpected path");
	zassert_equal(peer.content_format, COAP_CONTENT_FORMAT_APP_LINK_FORMAT,
		      "the deregistration must carry the link format");
}

ZTEST(sirena_lwm2m_coap, test_deregister_without_registration)
{
	int rc;

	rc = sirena_lwm2m_coap_set_endpoint_name(PEER_ENDPOINT_NAME);
	zassert_equal(rc, 0, "endpoint name rejected");

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, 0, "register failed");

	rc = sirena_lwm2m_transport_deregister();
	zassert_equal(rc, 0, "deregister failed");

	rc = sirena_lwm2m_transport_deregister();
	zassert_equal(rc, -EINVAL, "deregister without registration must fail");
}

ZTEST(sirena_lwm2m_coap, test_register_without_endpoint_name)
{
	int rc;

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, -EINVAL, "register without endpoint name must fail");
	zassert_false(peer.received, "no request must be sent");
}

ZTEST(sirena_lwm2m_coap, test_start_refuses_a_pre_shared_key_server)
{
	int rc;

	peer_provision_psk(server_uri);

	rc = sirena_lwm2m_coap_set_endpoint_name(PEER_ENDPOINT_NAME);
	zassert_equal(rc, 0, "endpoint name rejected");

	rc = sirena_lwm2m_coap_start();
	zassert_equal(rc, -EINVAL, "a pre-shared key server must be refused in the clear");

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, -EINVAL, "a pre-shared key server must not be contacted in the clear");
	zassert_false(peer.received, "no request must be sent");
}

ZTEST(sirena_lwm2m_coap, test_register_rejects_secure_scheme)
{
	int rc;

	rc = sirena_lwm2m_coap_set_endpoint_name(PEER_ENDPOINT_NAME);
	zassert_equal(rc, 0, "endpoint name rejected");

	peer_provision("coaps://127.0.0.1:5684");

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, -ENOTSUP, "secure scheme must be rejected");
	zassert_false(peer.received, "no request must be sent");
}

ZTEST(sirena_lwm2m_coap, test_register_reports_precondition_failed)
{
	int rc;

	rc = sirena_lwm2m_coap_set_endpoint_name(PEER_ENDPOINT_NAME);
	zassert_equal(rc, 0, "endpoint name rejected");

	peer_reject_register = true;

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, -EPROTO, "precondition failed must have its own error code");
	zassert_true(peer.received, "peer received no request");
}

ZTEST(sirena_lwm2m_coap, test_failed_register_stores_no_location_path)
{
	char location[64];
	size_t length = sizeof(location);
	int rc;

	rc = sirena_lwm2m_coap_set_endpoint_name(PEER_ENDPOINT_NAME);
	zassert_equal(rc, 0, "endpoint name rejected");

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, 0, "register failed");

	rc = sirena_lwm2m_transport_deregister();
	zassert_equal(rc, 0, "deregister failed");

	peer_reject_register = true;

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, -EPROTO, "precondition failed must have its own error code");

	rc = sirena_lwm2m_coap_get_location_path(location, &length);
	zassert_equal(rc, 0, "location path unavailable");
	zassert_str_equal(location, "", "a failed registration must not store a location path");
}
