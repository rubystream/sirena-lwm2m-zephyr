#include <zephyr/ztest.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/coap_service.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/sys/util.h>
#include <sirena/lwm2m/coap.h>
#include <sirena/lwm2m/dtls.h>
#include <sirena/lwm2m/engine.h>
#include <sirena/lwm2m/object_registry.h>
#include <sirena/lwm2m/objects.h>
#include <sirena/lwm2m/transport.h>
#include <mbedtls/ssl_ciphersuites.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define SECURITY_OBJECT_ID 0U
#define SECURITY_MODE_RESOURCE 2U

#define PEER_LIFETIME 300U
#define PEER_LOCATION "rd"
#define PEER_LOCATION_ID "1"
#define PEER_OPTION_MAX 16
#define PEER_QUERY_MAX 8
#define PEER_QUERY_LEN 72
#define PEER_PATH_LEN 64
#define PEER_PAYLOAD_LEN 320
#define PEER_BUFFER_LEN 512
#define PEER_ENDPOINT_NAME "sirena-test"
#define PEER_DTLS_PORT 5684
#define PEER_SEC_TAG 2
#define PEER_PSK_IDENTITY "sirena-psk-identity"
#define PEER_PSK_KEY "0123456789abcdef"
#define PEER_OTHER_PSK_IDENTITY "other-psk-identity"
#define PEER_OTHER_PSK_KEY "fedcba9876543210"

struct peer_request {
	bool received;
	uint8_t method;
	char path[PEER_PATH_LEN];
	uint8_t query_count;
	char query[PEER_QUERY_MAX][PEER_QUERY_LEN];
	int content_format;
	char payload[PEER_PAYLOAD_LEN];
};

static struct peer_request dtls_request;
static struct peer_request plain_request;
static char dtls_uri[64];
static char dtls_uri_without_port[64];
static char plain_uri[64];
static char custom_server_uri[64];
static int64_t custom_security_mode;
static k_tid_t security_read_thread;
static int dtls_peer_socket = -1;
static uint16_t peer_port;
static uint16_t dtls_peer_port;

K_THREAD_STACK_DEFINE(dtls_peer_stack, 4096);
static struct k_thread dtls_peer_thread;

static void peer_append_path(struct peer_request *record, const uint8_t *value, uint16_t length)
{
	size_t used = strlen(record->path);

	if (used + length + 2U > sizeof(record->path)) {
		return;
	}

	record->path[used] = '/';
	memcpy(&record->path[used + 1U], value, length);
	record->path[used + 1U + length] = '\0';
}

static void peer_record(struct peer_request *record, struct coap_packet *packet, uint8_t method)
{
	struct coap_option options[PEER_OPTION_MAX];
	const uint8_t *payload;
	uint16_t payload_length;
	int count;
	int i;

	memset(record, 0, sizeof(*record));
	record->received = true;
	record->method = method;
	record->content_format = coap_get_option_int(packet, COAP_OPTION_CONTENT_FORMAT);

	count = coap_find_options(packet, COAP_OPTION_URI_PATH, options, ARRAY_SIZE(options));
	for (i = 0; i < count; ++i) {
		peer_append_path(record, options[i].value, options[i].len);
	}

	count = coap_find_options(packet, COAP_OPTION_URI_QUERY, options, ARRAY_SIZE(options));
	for (i = 0; i < count && record->query_count < PEER_QUERY_MAX; ++i) {
		size_t length = options[i].len;

		if (length >= PEER_QUERY_LEN) {
			continue;
		}

		memcpy(record->query[record->query_count], options[i].value, length);
	(record->query[record->query_count])[length] = '\0';
		record->query_count++;
	}

	payload = coap_packet_get_payload(packet, &payload_length);
	if (payload != NULL && payload_length < sizeof(record->payload)) {
		memcpy(record->payload, payload, payload_length);
		record->payload[payload_length] = '\0';
	}
}

static bool peer_has_query(const struct peer_request *record, const char *value)
{
	uint8_t i;

	for (i = 0; i < record->query_count; ++i) {
		if (strcmp(record->query[i], value) == 0) {
			return true;
		}
	}

	return false;
}

static int peer_reply(struct coap_resource *resource, struct coap_packet *request,
		       struct net_sockaddr *addr, net_socklen_t addr_len, uint8_t code)
{
	static const char *const location[] = { PEER_LOCATION, PEER_LOCATION_ID, NULL };
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

	for (i = 0; location[i] != NULL; ++i) {
		rc = coap_packet_append_option(&response, COAP_OPTION_LOCATION_PATH,
					       (const uint8_t *)location[i],
					       (uint16_t)strlen(location[i]));
		if (rc < 0) {
			return rc;
		}
	}

	return coap_resource_send(resource, &response, addr, addr_len, NULL);
}

static int plain_post(struct coap_resource *resource, struct coap_packet *request,
		      struct net_sockaddr *addr, net_socklen_t addr_len)
{
	peer_record(&plain_request, request, COAP_METHOD_POST);

	return peer_reply(resource, request, addr, addr_len, COAP_RESPONSE_CODE_CREATED);
}

COAP_SERVICE_DEFINE(peer_service, "127.0.0.1", &peer_port, 0);

static const char *const plain_register_path[] = { PEER_LOCATION, NULL };
COAP_RESOURCE_DEFINE(peer_register_resource, peer_service, {
	.path = plain_register_path,
	.post = plain_post,
});

static void dtls_peer_reply(struct coap_packet *request, struct net_sockaddr *addr,
			     net_socklen_t addr_len)
{
	static const char *const location[] = { PEER_LOCATION, PEER_LOCATION_ID };
	uint8_t buffer[64];
	struct coap_packet response;
	int rc;
	int i;

	rc = coap_ack_init(&response, request, buffer, sizeof(buffer), COAP_RESPONSE_CODE_CREATED);
	if (rc < 0) {
		return;
	}

	for (i = 0; i < 2; ++i) {
		rc = coap_packet_append_option(&response, COAP_OPTION_LOCATION_PATH,
					       (const uint8_t *)location[i],
					       (uint16_t)strlen(location[i]));
		if (rc < 0) {
			return;
		}
	}

	(void)zsock_sendto(dtls_peer_socket, response.data, response.offset, 0, addr, addr_len);
}

static void dtls_peer_entry(void *first, void *second, void *third)
{
	uint8_t buffer[PEER_BUFFER_LEN];

	(void)first;
	(void)second;
	(void)third;

	while (true) {
		struct net_sockaddr_in peer_address;
		net_socklen_t peer_length = sizeof(peer_address);
		struct coap_packet request;
		struct coap_option options[PEER_OPTION_MAX];
		ssize_t length;
		int rc;

		length = zsock_recvfrom(dtls_peer_socket, buffer, sizeof(buffer), 0,
					 (struct net_sockaddr *)&peer_address, &peer_length);
		if (length <= 0) {
			continue;
		}

		rc = coap_packet_parse(&request, buffer, (uint16_t)length, options,
				       ARRAY_SIZE(options));
		if (rc < 0) {
			continue;
		}

		peer_record(&dtls_request, &request, buffer[1] & 0x07U);

		dtls_peer_reply(&request, (struct net_sockaddr *)&peer_address, peer_length);
	}
}

static void dtls_peer_start(void)
{
	static const int ciphersuites[] = { MBEDTLS_TLS_PSK_WITH_AES_128_CCM_8 };
	sec_tag_t tags[] = { PEER_SEC_TAG };
	struct net_sockaddr_in address;
	struct net_sockaddr_in bound;
	net_socklen_t length = sizeof(bound);
	int verify = ZSOCK_TLS_PEER_VERIFY_NONE;
	int role = ZSOCK_TLS_DTLS_ROLE_SERVER;
	int rc;

	rc = tls_credential_add(PEER_SEC_TAG, TLS_CREDENTIAL_PSK, PEER_PSK_KEY,
				sizeof(PEER_PSK_KEY) - 1U);
	zassert_equal(rc, 0, "peer key registration failed: %d", rc);

	rc = tls_credential_add(PEER_SEC_TAG, TLS_CREDENTIAL_PSK_ID, PEER_PSK_IDENTITY,
				sizeof(PEER_PSK_IDENTITY) - 1U);
	zassert_equal(rc, 0, "peer identity registration failed: %d", rc);

	dtls_peer_socket = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_DTLS_1_2);
	zassert_true(dtls_peer_socket >= 0, "peer socket creation failed");

	rc = zsock_setsockopt(dtls_peer_socket, ZSOCK_SOL_TLS, ZSOCK_TLS_DTLS_ROLE, &role,
			      sizeof(role));
	zassert_equal(rc, 0, "peer role setup failed: %d", rc);

	rc = zsock_setsockopt(dtls_peer_socket, ZSOCK_SOL_TLS, ZSOCK_TLS_SEC_TAG_LIST, tags,
			      sizeof(tags));
	zassert_equal(rc, 0, "peer credential setup failed: %d", rc);

	rc = zsock_setsockopt(dtls_peer_socket, ZSOCK_SOL_TLS, ZSOCK_TLS_CIPHERSUITE_LIST,
			      ciphersuites, sizeof(ciphersuites));
	zassert_equal(rc, 0, "peer ciphersuite setup failed: %d", rc);

	rc = zsock_setsockopt(dtls_peer_socket, ZSOCK_SOL_TLS, ZSOCK_TLS_PEER_VERIFY, &verify,
			      sizeof(verify));
	zassert_equal(rc, 0, "peer verification setup failed: %d", rc);

	memset(&address, 0, sizeof(address));
	address.sin_family = NET_AF_INET;
	address.sin_port = net_htons(PEER_DTLS_PORT);
	zassert_equal(zsock_inet_pton(NET_AF_INET, "127.0.0.1", &address.sin_addr), 1,
		      "peer address setup failed");

	rc = zsock_bind(dtls_peer_socket, (struct net_sockaddr *)&address, sizeof(address));
	zassert_equal(rc, 0, "peer bind failed: %d", rc);

	length = sizeof(bound);
	rc = zsock_getsockname(dtls_peer_socket, (struct net_sockaddr *)&bound, &length);
	zassert_equal(rc, 0, "peer port lookup failed: %d", rc);
	dtls_peer_port = bound.sin_port;

	k_thread_create(&dtls_peer_thread, dtls_peer_stack, K_THREAD_STACK_SIZEOF(dtls_peer_stack),
			dtls_peer_entry, NULL, NULL, NULL, K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
}

static void provision_psk(const char *uri, const char *identity, size_t identity_length,
			   const char *secret_key, size_t secret_key_length)
{
	struct sirena_lwm2m_provisioning provisioning = {
		.server_uri = uri,
		.security_mode = SIRENA_LWM2M_SECURITY_MODE_PSK,
		.identity = identity,
		.identity_length = identity_length,
		.secret_key = secret_key,
		.secret_key_length = secret_key_length,
		.short_server_id = 1U,
		.lifetime = PEER_LIFETIME,
		.binding = "U",
	};
	int rc;

	rc = sirena_lwm2m_provision(&provisioning);
	zassert_equal(rc, 0, "provisioning failed: %d", rc);
}

static void provision_nosec(const char *uri)
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
	zassert_equal(rc, 0, "provisioning failed: %d", rc);
}

static int custom_read_string(uint16_t object_id, uint16_t instance_id, uint16_t resource_id,
			      void *user_data, void *buffer, size_t *length)
{
	size_t used = strlen(custom_server_uri);

	(void)object_id;
	(void)instance_id;
	(void)resource_id;
	(void)user_data;

	security_read_thread = k_current_get();

	if (buffer == NULL || (length != NULL && *length < used)) {
		return -ENOMEM;
	}

	memcpy(buffer, custom_server_uri, used);
	if (length != NULL) {
		*length = used;
	}

	return 0;
}

static int custom_read_integer(uint16_t object_id, uint16_t instance_id, uint16_t resource_id,
			       void *user_data, void *buffer, size_t *length)
{
	(void)object_id;
	(void)instance_id;
	(void)resource_id;
	(void)user_data;

	security_read_thread = k_current_get();

	if (buffer == NULL || length == NULL || *length < sizeof(custom_security_mode)) {
		return -ENOMEM;
	}

	memcpy(buffer, &custom_security_mode, sizeof(custom_security_mode));
	*length = sizeof(custom_security_mode);

	return 0;
}

static void custom_security_object(const char *uri, int64_t mode)
{
	int rc;

	rc = snprintf(custom_server_uri, sizeof(custom_server_uri), "%s", uri);
	zassert_true(rc > 0 && rc < (int)sizeof(custom_server_uri), "custom uri build failed");

	custom_security_mode = mode;

	rc = sirena_lwm2m_registry_init();
	zassert_equal(rc, 0, "registry init failed");

	rc = sirena_lwm2m_register_object(SECURITY_OBJECT_ID, false);
	zassert_equal(rc, 0, "security object registration failed");

	rc = sirena_lwm2m_create_instance(SECURITY_OBJECT_ID, 0U, NULL);
	zassert_equal(rc, 0, "security instance creation failed");

	rc = sirena_lwm2m_add_resource(SECURITY_OBJECT_ID, 0U, 0U, SIRENA_LWM2M_VALUE_STRING,
				       SIRENA_LWM2M_OP_READ, false, custom_read_string, NULL, NULL,
				       NULL);
	zassert_equal(rc, 0, "server uri resource registration failed");

	rc = sirena_lwm2m_add_resource(SECURITY_OBJECT_ID, 0U, SECURITY_MODE_RESOURCE,
				       SIRENA_LWM2M_VALUE_INTEGER, SIRENA_LWM2M_OP_READ, false,
				       custom_read_integer, NULL, NULL, NULL);
	zassert_equal(rc, 0, "security mode resource registration failed");
}

static void *suite_setup(void)
{
	struct net_sockaddr_in address;
	net_socklen_t length = sizeof(address);
	int rc;

	rc = coap_service_start(&peer_service);
	zassert_equal(rc, 0, "plain peer service start failed: %d", rc);

	memset(&address, 0, sizeof(address));
	address.sin_family = NET_AF_INET;
	rc = zsock_getsockname(peer_service.data->sock_fd, (struct net_sockaddr *)&address,
			       &length);
	zassert_equal(rc, 0, "plain peer port lookup failed: %d", rc);

	rc = snprintf(plain_uri, sizeof(plain_uri), "coap://127.0.0.1:%u",
		      net_ntohs(address.sin_port));
	zassert_true(rc > 0 && rc < (int)sizeof(plain_uri), "plain uri build failed");

	rc = snprintf(dtls_uri, sizeof(dtls_uri), "coaps://127.0.0.1:%u", PEER_DTLS_PORT);
	zassert_true(rc > 0 && rc < (int)sizeof(dtls_uri), "dtls uri build failed");

	rc = snprintf(dtls_uri_without_port, sizeof(dtls_uri_without_port), "coaps://127.0.0.1");
	zassert_true(rc > 0 && rc < (int)sizeof(dtls_uri_without_port), "dtls uri build failed");

	dtls_peer_start();

	return NULL;
}

static void test_before(void *fixture)
{
	int rc;

	(void)fixture;

	memset(&dtls_request, 0, sizeof(dtls_request));
	memset(&plain_request, 0, sizeof(plain_request));

	rc = sirena_lwm2m_registry_init();
	zassert_equal(rc, 0, "registry init failed");

	rc = sirena_lwm2m_objects_init();
	zassert_equal(rc, 0, "objects init failed");

	rc = sirena_lwm2m_transport_set_endpoint_name(PEER_ENDPOINT_NAME);
	zassert_equal(rc, 0, "endpoint name rejected");
}

ZTEST_SUITE(sirena_lwm2m_dtls, NULL, suite_setup, test_before, NULL, NULL);

ZTEST(sirena_lwm2m_dtls, test_register_sends_expected_request)
{
	int rc;

	provision_psk(dtls_uri, PEER_PSK_IDENTITY, sizeof(PEER_PSK_IDENTITY) - 1U, PEER_PSK_KEY,
		      sizeof(PEER_PSK_KEY) - 1U);

	rc = sirena_lwm2m_transport_start();
	zassert_equal(rc, 0, "transport start failed: %d", rc);

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, 0, "register failed: %d", rc);

	zassert_true(dtls_request.received, "the peer received no request");
	zassert_equal(dtls_request.method, COAP_METHOD_POST, "unexpected method");
	zassert_str_equal(dtls_request.path, "/rd", "unexpected path");
	zassert_equal(dtls_request.query_count, 4, "the query must carry one option per parameter");
	zassert_true(peer_has_query(&dtls_request, "ep=sirena-test"), "missing endpoint name");
	zassert_true(peer_has_query(&dtls_request, "lt=300"), "missing lifetime");
	zassert_true(peer_has_query(&dtls_request, "lwm2m=1.2"), "missing lwm2m version");
	zassert_true(peer_has_query(&dtls_request, "b=U"), "missing binding mode");
	zassert_equal(dtls_request.content_format, COAP_CONTENT_FORMAT_APP_LINK_FORMAT,
		      "unexpected content format");
	zassert_str_equal(dtls_request.payload, "</1/0>,</3/0>", "unexpected registration payload");
	zassert_true(strstr(dtls_request.payload, "</0/0>") == NULL,
		      "the security object must not be listed");
}

ZTEST(sirena_lwm2m_dtls, test_the_plaintext_binding_refuses_a_pre_shared_key_server)
{
	int rc;

	provision_psk(plain_uri, PEER_PSK_IDENTITY, sizeof(PEER_PSK_IDENTITY) - 1U, PEER_PSK_KEY,
		      sizeof(PEER_PSK_KEY) - 1U);

	rc = sirena_lwm2m_coap_start();
	zassert_equal(rc, -EINVAL, "a pre-shared key server must be refused in the clear");
	zassert_false(plain_request.received, "no request must reach the server");
}

ZTEST(sirena_lwm2m_dtls, test_the_plaintext_binding_requires_the_noSec_gate)
{
	int rc;

	provision_nosec(plain_uri);

	rc = sirena_lwm2m_coap_start();
#if IS_ENABLED(CONFIG_SIRENA_LWM2M_TRANSPORT_NOSEC)
	zassert_equal(rc, 0, "the noSec gate is open: %d", rc);

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, 0, "register failed: %d", rc);

	zassert_true(plain_request.received, "the plain peer received no request");
	zassert_false(dtls_request.received, "the dtls peer must not receive the request");
#else
	zassert_equal(rc, -EPERM, "the closed noSec gate must refuse a noSec server");
	zassert_false(plain_request.received, "no request must be sent");
#endif
}

ZTEST(sirena_lwm2m_dtls, test_the_security_object_is_read_in_the_engine_thread)
{
	k_tid_t application_thread = k_current_get();
	k_tid_t engine_thread;
	int rc;

	custom_security_object(dtls_uri, SIRENA_LWM2M_SECURITY_MODE_PSK);

	security_read_thread = NULL;

	rc = sirena_lwm2m_transport_start();
	zassert_equal(rc, 0, "transport start failed: %d", rc);

	engine_thread = sirena_lwm2m_engine_thread_id();
	zassert_not_null(engine_thread, "the engine thread is not running");
	zassert_equal(security_read_thread, engine_thread,
		      "the security object must be read in the engine thread");
	zassert_not_equal(security_read_thread, application_thread,
			  "the security object must not be read in the application thread");
}

ZTEST(sirena_lwm2m_dtls, test_dtls_binding_uses_the_secure_port)
{
	int rc;

	provision_psk(dtls_uri, PEER_PSK_IDENTITY, sizeof(PEER_PSK_IDENTITY) - 1U, PEER_PSK_KEY,
		      sizeof(PEER_PSK_KEY) - 1U);

	rc = sirena_lwm2m_transport_start();
	zassert_equal(rc, 0, "transport start failed: %d", rc);

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, 0, "register failed: %d", rc);

	zassert_true(dtls_request.received, "the peer received no request");
	zassert_equal(net_ntohs(dtls_peer_port), PEER_DTLS_PORT,
		      "the secure binding must use port 5684");
}

ZTEST(sirena_lwm2m_dtls, test_the_secure_binding_uses_the_default_port)
{
	int rc;

	provision_psk(dtls_uri_without_port, PEER_PSK_IDENTITY, sizeof(PEER_PSK_IDENTITY) - 1U,
		      PEER_PSK_KEY, sizeof(PEER_PSK_KEY) - 1U);

	rc = sirena_lwm2m_transport_start();
	zassert_equal(rc, 0, "transport start failed: %d", rc);

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, 0, "register failed: %d", rc);

	zassert_true(dtls_request.received, "the peer received no request");
	zassert_equal(net_ntohs(dtls_peer_port), PEER_DTLS_PORT,
		      "the secure binding must use the default port 5684");
}

ZTEST(sirena_lwm2m_dtls, test_register_stores_the_location_path)
{
	char location[64];
	size_t length = sizeof(location);
	int rc;

	provision_psk(dtls_uri, PEER_PSK_IDENTITY, sizeof(PEER_PSK_IDENTITY) - 1U, PEER_PSK_KEY,
		      sizeof(PEER_PSK_KEY) - 1U);

	rc = sirena_lwm2m_transport_start();
	zassert_equal(rc, 0, "transport start failed: %d", rc);

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, 0, "register failed: %d", rc);

	rc = sirena_lwm2m_dtls_get_location_path(location, &length);
	zassert_equal(rc, 0, "location path unavailable");
	zassert_str_equal(location, "/rd/1", "unexpected location path");
}

ZTEST(sirena_lwm2m_dtls, test_register_fails_with_a_wrong_psk)
{
	int rc;

	provision_psk(dtls_uri, PEER_PSK_IDENTITY, sizeof(PEER_PSK_IDENTITY) - 1U,
		      PEER_OTHER_PSK_KEY, sizeof(PEER_OTHER_PSK_KEY) - 1U);

	rc = sirena_lwm2m_transport_start();
	zassert_equal(rc, 0, "transport start failed: %d", rc);

	rc = sirena_lwm2m_transport_register();
	zassert_not_equal(rc, 0, "the handshake must fail with a wrong pre-shared key");
	zassert_false(dtls_request.received, "no request must reach the server");
}

ZTEST(sirena_lwm2m_dtls, test_register_fails_with_an_unknown_psk_identity)
{
	int rc;

	provision_psk(dtls_uri, PEER_OTHER_PSK_IDENTITY, sizeof(PEER_OTHER_PSK_IDENTITY) - 1U,
		      PEER_PSK_KEY, sizeof(PEER_PSK_KEY) - 1U);

	rc = sirena_lwm2m_transport_start();
	zassert_equal(rc, 0, "transport start failed: %d", rc);

	rc = sirena_lwm2m_transport_register();
	zassert_not_equal(rc, 0, "the handshake must fail with an unknown pre-shared key identity");
	zassert_false(dtls_request.received, "no request must reach the server");
}

ZTEST(sirena_lwm2m_dtls, test_transport_refuses_the_unsupported_security_modes)
{
	static const int64_t modes[] = { SIRENA_LWM2M_SECURITY_MODE_RPK,
					 SIRENA_LWM2M_SECURITY_MODE_CERTIFICATE,
					 SIRENA_LWM2M_SECURITY_MODE_CERTIFICATE_EST };
	size_t i;
	int rc;

	provision_psk(dtls_uri, PEER_PSK_IDENTITY, sizeof(PEER_PSK_IDENTITY) - 1U, PEER_PSK_KEY,
		      sizeof(PEER_PSK_KEY) - 1U);

	for (i = 0; i < ARRAY_SIZE(modes); ++i) {
		rc = sirena_lwm2m_set_integer(SECURITY_OBJECT_ID, 0U, SECURITY_MODE_RESOURCE,
					      modes[i]);
		zassert_equal(rc, 0, "security mode update failed: %d", rc);

		rc = sirena_lwm2m_transport_start();
		zassert_equal(rc, -ENOTSUP, "the unsupported security mode must be refused");
	}

	zassert_false(dtls_request.received, "no request must be sent");
}

ZTEST(sirena_lwm2m_dtls, test_the_dtls_binding_refuses_a_noSec_server)
{
	int rc;

	provision_nosec(dtls_uri);

	rc = sirena_lwm2m_dtls_start();
	zassert_equal(rc, -EINVAL, "a noSec server must be refused by the secure binding");
	zassert_false(dtls_request.received, "no request must be sent");
}

ZTEST(sirena_lwm2m_dtls, test_register_fails_without_the_credentials)
{
	int rc;

	provision_psk(dtls_uri, NULL, 0U, PEER_PSK_KEY, sizeof(PEER_PSK_KEY) - 1U);

	rc = sirena_lwm2m_transport_start();
	zassert_equal(rc, 0, "transport start failed: %d", rc);

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, -ENOENT, "a missing pre-shared key identity must be refused");
	zassert_false(dtls_request.received, "no request must be sent");

	provision_psk(dtls_uri, PEER_PSK_IDENTITY, sizeof(PEER_PSK_IDENTITY) - 1U, NULL, 0U);

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, -ENOENT, "a missing pre-shared key must be refused");
	zassert_false(dtls_request.received, "no request must be sent");
}

ZTEST(sirena_lwm2m_dtls, test_transport_selects_the_binding_from_the_server_uri)
{
	int rc;

	provision_psk(dtls_uri, PEER_PSK_IDENTITY, sizeof(PEER_PSK_IDENTITY) - 1U, PEER_PSK_KEY,
		      sizeof(PEER_PSK_KEY) - 1U);

	rc = sirena_lwm2m_transport_start();
	zassert_equal(rc, 0, "the secure scheme must select the dtls binding: %d", rc);

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, 0, "register failed: %d", rc);

	zassert_true(dtls_request.received, "the dtls peer received no request");
	zassert_false(plain_request.received, "the plain peer must not receive the request");

	provision_psk(plain_uri, PEER_PSK_IDENTITY, sizeof(PEER_PSK_IDENTITY) - 1U, PEER_PSK_KEY,
		      sizeof(PEER_PSK_KEY) - 1U);

	rc = sirena_lwm2m_transport_start();
	zassert_equal(rc, -EINVAL, "the plain scheme must not be used with a pre-shared key server");
}

ZTEST(sirena_lwm2m_dtls, test_nosec_requires_the_development_gate)
{
	int rc;

	provision_nosec(plain_uri);

	rc = sirena_lwm2m_transport_start();
#if IS_ENABLED(CONFIG_SIRENA_LWM2M_TRANSPORT_NOSEC)
	zassert_equal(rc, 0, "the noSec gate is open: %d", rc);

	rc = sirena_lwm2m_transport_register();
	zassert_equal(rc, 0, "register failed: %d", rc);

	zassert_true(plain_request.received, "the plain peer received no request");
	zassert_false(dtls_request.received, "the dtls peer must not receive the request");
#else
	zassert_equal(rc, -EPERM, "the closed noSec gate must refuse a noSec server");
	zassert_false(plain_request.received, "no request must be sent");
#endif
}
