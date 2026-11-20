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

struct peer_request {
	bool received;
	uint8_t method;
	char path[64];
	char query[192];
	int content_format;
	char payload[320];
};

static struct peer_request peer;
static bool peer_reject_register;
static uint16_t peer_port;
static char server_uri[64];

static void peer_record(struct coap_packet *request, uint8_t method)
{
	struct coap_option options[8];
	const uint8_t *payload;
	uint16_t payload_length;
	size_t length;
	int count;
	int i;

	memset(&peer, 0, sizeof(peer));
	peer.received = true;
	peer.method = method;
	peer.content_format = coap_get_option_int(request, COAP_OPTION_CONTENT_FORMAT);

	count = coap_find_options(request, COAP_OPTION_URI_PATH, options, ARRAY_SIZE(options));
	for (i = 0; i < count; ++i) {
		length = strlen(peer.path);
		if (length + options[i].len + 2U > sizeof(peer.path)) {
			break;
		}

		peer.path[length] = '/';
		memcpy(&peer.path[length + 1U], options[i].value, options[i].len);
		peer.path[length + 1U + options[i].len] = '\0';
	}

	count = coap_find_options(request, COAP_OPTION_URI_QUERY, options, ARRAY_SIZE(options));
	for (i = 0; i < count; ++i) {
		length = strlen(peer.query);
		if (length + options[i].len + 2U > sizeof(peer.query)) {
			break;
		}

		if (length > 0U) {
			peer.query[length++] = '&';
		}

		memcpy(&peer.query[length], options[i].value, options[i].len);
		length += options[i].len;
		peer.query[length] = '\0';
	}

	payload = coap_packet_get_payload(request, &payload_length);
	if (payload != NULL && payload_length < sizeof(peer.payload)) {
		memcpy(peer.payload, payload, payload_length);
		peer.payload[payload_length] = '\0';
	}
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
				  COAP_RESPONSE_CODE_PRECONDITION_FAILED, NULL);
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

	rc = sirena_lwm2m_coap_start();
	zassert_equal(rc, 0, "transport start failed: %d", rc);

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
}

ZTEST_SUITE(sirena_lwm2m_coap, NULL, suite_setup, test_before, NULL, NULL);

ZTEST(sirena_lwm2m_coap, test_register_sends_expected_request)
{
	int rc;

	rc = sirena_lwm2m_coap_set_endpoint_name("sirena-test");
	zassert_equal(rc, 0, "endpoint name rejected");

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, 0, "register failed");

	zassert_true(peer.received, "peer received no request");
	zassert_equal(peer.method, COAP_METHOD_POST, "unexpected method");
	zassert_str_equal(peer.path, "/rd", "unexpected path");
	zassert_not_null(strstr(peer.query, "ep=sirena-test"), "missing endpoint name");
	zassert_not_null(strstr(peer.query, "lt=300"), "missing lifetime");
	zassert_not_null(strstr(peer.query, "lwm2m=1.2"), "missing lwm2m version");
	zassert_not_null(strstr(peer.query, "b=U"), "missing binding mode");
	zassert_equal(peer.content_format, COAP_CONTENT_FORMAT_APP_LINK_FORMAT,
		      "unexpected content format");
	zassert_not_null(strstr(peer.payload, "</1/0>"), "missing server instance");
	zassert_not_null(strstr(peer.payload, "</3/0>"), "missing device instance");
	zassert_is_null(strstr(peer.payload, "</0/0>"), "security object must not be listed");
}

ZTEST(sirena_lwm2m_coap, test_register_payload_follows_registry)
{
	int rc;

	rc = sirena_lwm2m_coap_set_endpoint_name("sirena-test");
	zassert_equal(rc, 0, "endpoint name rejected");

	rc = sirena_lwm2m_create_instance(3U, 1U, NULL);
	zassert_equal(rc, 0, "instance creation failed");

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, 0, "register failed");

	zassert_not_null(strstr(peer.payload, "</3/1>"), "missing extra instance");
	zassert_not_null(strstr(peer.payload, "</3/0>"), "missing device instance");
}

ZTEST(sirena_lwm2m_coap, test_register_stores_location_path)
{
	char location[64];
	size_t length = sizeof(location);
	int rc;

	rc = sirena_lwm2m_coap_set_endpoint_name("sirena-test");
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

	rc = sirena_lwm2m_coap_set_endpoint_name("sirena-test");
	zassert_equal(rc, 0, "endpoint name rejected");

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, 0, "register failed");

	rc = sirena_lwm2m_transport_update();
	zassert_equal(rc, 0, "update failed");

	zassert_true(peer.received, "peer received no request");
	zassert_equal(peer.method, COAP_METHOD_POST, "unexpected method");
	zassert_str_equal(peer.path, "/rd/1", "unexpected path");
	zassert_not_null(strstr(peer.query, "lt=300"), "missing lifetime");
	zassert_not_null(strstr(peer.query, "b=U"), "missing binding mode");
	zassert_is_null(strstr(peer.query, "ep="), "endpoint name must not be sent on update");
}

ZTEST(sirena_lwm2m_coap, test_deregister_uses_location_path)
{
	int rc;

	rc = sirena_lwm2m_coap_set_endpoint_name("sirena-test");
	zassert_equal(rc, 0, "endpoint name rejected");

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, 0, "register failed");

	rc = sirena_lwm2m_transport_deregister();
	zassert_equal(rc, 0, "deregister failed");

	zassert_true(peer.received, "peer received no request");
	zassert_equal(peer.method, COAP_METHOD_DELETE, "unexpected method");
	zassert_str_equal(peer.path, "/rd/1", "unexpected path");
}

ZTEST(sirena_lwm2m_coap, test_deregister_without_registration)
{
	int rc;

	rc = sirena_lwm2m_transport_deregister();
	if (rc == 0) {
		rc = sirena_lwm2m_transport_deregister();
	}

	zassert_equal(rc, -EINVAL, "deregister without registration must fail");
}

ZTEST(sirena_lwm2m_coap, test_register_without_endpoint_name)
{
	int rc;

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, -EINVAL, "register without endpoint name must fail");
	zassert_false(peer.received, "no request must be sent");
}

ZTEST(sirena_lwm2m_coap, test_register_rejects_secure_scheme)
{
	int rc;

	rc = sirena_lwm2m_coap_set_endpoint_name("sirena-test");
	zassert_equal(rc, 0, "endpoint name rejected");

	peer_provision("coaps://127.0.0.1:5684");

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, -ENOTSUP, "secure scheme must be rejected");
	zassert_false(peer.received, "no request must be sent");
}

ZTEST(sirena_lwm2m_coap, test_register_reports_precondition_failed)
{
	int rc;

	rc = sirena_lwm2m_coap_set_endpoint_name("sirena-test");
	zassert_equal(rc, 0, "endpoint name rejected");

	peer_reject_register = true;

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, -EPROTO, "precondition failed must have its own error code");
	zassert_true(peer.received, "peer received no request");
}
