#include <zephyr/ztest.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/coap_client.h>
#include <zephyr/net/coap_service.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>
#include "sirena_lwm2m_coap_endpoint.h"
#include <sirena/lwm2m/codec.h>
#include <sirena/lwm2m/coap.h>
#include <sirena/lwm2m/object_registry.h>
#include <sirena/lwm2m/objects.h>
#include <sirena/lwm2m/protocol.h>
#include <sirena/lwm2m/transport.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define PEER_LIFETIME 300U
#define PEER_LIFETIME_UPDATED 600U
#define PEER_LIFETIME_NOTIFIED 900U
#define PEER_ENDPOINT_NAME "sirena-protocol-test"

#define PEER_SECURITY_OBJECT_ID 0U
#define PEER_SERVER_OBJECT_ID 1U
#define PEER_DEVICE_OBJECT_ID 3U
#define PEER_INSTANCE_ID 0U

#define PEER_SERVER_URI_RESOURCE 0U
#define PEER_SERVER_LIFETIME_RESOURCE 1U
#define PEER_SERVER_EXECUTE_RESOURCE 8U
#define PEER_DEVICE_REBOOT_RESOURCE 4U

#define PEER_LOCATION "rd"
#define PEER_LOCATION_ID "1"
#define PEER_PAYLOAD_MAX 256

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

struct guarded_path {
    uint16_t path[3];
    uint16_t guard[3];
};

static struct peer_capture peer_capture;
static struct net_sockaddr_storage peer_client_address;
static net_socklen_t peer_client_address_length;
static bool peer_registered;
static uint16_t peer_port;
static char server_uri[64];
static struct coap_client peer_client;
static int peer_socket = -1;
static K_SEM_DEFINE(peer_semaphore, 0, 64);

static bool peer_execute_called;
static uint16_t peer_execute_object_id;
static uint16_t peer_execute_resource_id;

static void peer_execute_handler(uint16_t object_id, uint16_t instance_id,
                                 uint16_t resource_id, void *user_data)
{
    (void)instance_id;
    (void)user_data;

    peer_execute_called = true;
    peer_execute_object_id = object_id;
    peer_execute_resource_id = resource_id;
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

    peer_registered = true;
    memcpy(&peer_client_address, addr, sizeof(peer_client_address));
    peer_client_address_length = addr_len;

    return peer_reply(resource, request, addr, addr_len, COAP_RESPONSE_CODE_CREATED, location);
}

COAP_SERVICE_DEFINE(peer_service, "127.0.0.1", &peer_port, 0);

static const char *const peer_register_path[] = { PEER_LOCATION, NULL };
COAP_RESOURCE_DEFINE(peer_register_resource, peer_service, {
    .path = peer_register_path,
    .post = peer_register_post,
});

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
        peer_capture.content_format = coap_get_option_int(data->packet, COAP_OPTION_CONTENT_FORMAT);
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
                         const void *payload, size_t payload_length, const uint16_t *accept,
                         size_t accept_count, int observe, struct coap_client_request *request)
{
    struct coap_client_request req = {0};
    size_t i;
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

    for (i = 0U; i < accept_count; ++i) {
        req.options[req.num_options].code = COAP_OPTION_ACCEPT;
        req.options[req.num_options].len = 2U;
        req.options[req.num_options].value[0] = (uint8_t)(accept[i] >> 8);
        req.options[req.num_options].value[1] = (uint8_t)(accept[i] & 0xFFU);
        req.num_options++;
    }

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

    if (request != NULL) {
        *request = req;
    }

    rc = k_sem_take(&peer_semaphore, K_SECONDS(10));
    if (rc != 0) {
        return -ETIMEDOUT;
    }

    return 0;
}

static int peer_register(void)
{
    int rc;

    rc = sirena_lwm2m_coap_set_endpoint_name(PEER_ENDPOINT_NAME);
    zassert_equal(rc, 0, "endpoint name rejected");

    rc = sirena_lwm2m_transport_register();
    zassert_equal(rc, 0, "registration failed: %d", rc);
    zassert_true(peer_registered, "the peer received no registration");

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

static int64_t peer_decode_integer(uint16_t *content_format)
{
    struct sirena_lwm2m_codec_value values[4];
    size_t count = ARRAY_SIZE(values);
    int rc;

    rc = sirena_lwm2m_codec_decode(SIRENA_LWM2M_CF_TLV, peer_capture.payload,
                                    peer_capture.payload_length, values, &count);
    zassert_equal(rc, 0, "the tlv decoding failed: %d", rc);
    zassert_equal(count, 1U, "unexpected value count");
    zassert_equal(values[0].type, SIRENA_LWM2M_VALUE_INTEGER, "unexpected value type");

    if (content_format != NULL) {
        *content_format = peer_capture.content_format;
    }

    return values[0].data.integer;
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

    rc = coap_client_init(&peer_client, "sirena_lwm2m_test_peer");
    zassert_equal(rc, 0, "peer client init failed: %d", rc);

    return NULL;
}

static void test_before(void *fixture)
{
    struct sirena_lwm2m_provisioning provisioning = {
        .server_uri = server_uri,
        .security_mode = SIRENA_LWM2M_SECURITY_MODE_NOSEC,
        .short_server_id = 1U,
        .lifetime = PEER_LIFETIME,
        .binding = "U",
    };
    int rc;

    (void)fixture;

    memset(&peer_capture, 0, sizeof(peer_capture));
    peer_registered = false;
    peer_execute_called = false;
    peer_execute_object_id = 0U;
    peer_execute_resource_id = 0U;
    k_sem_reset(&peer_semaphore);

    rc = sirena_lwm2m_coap_stop();
    zassert_equal(rc, 0, "transport stop failed: %d", rc);

    rc = sirena_lwm2m_registry_init();
    zassert_equal(rc, 0, "registry init failed");

    rc = sirena_lwm2m_objects_init();
    zassert_equal(rc, 0, "objects init failed");

    rc = sirena_lwm2m_set_execute_handler(peer_execute_handler, NULL);
    zassert_equal(rc, 0, "execute handler registration failed");

    rc = sirena_lwm2m_provision(&provisioning);
    zassert_equal(rc, 0, "provisioning failed: %d", rc);

    rc = sirena_lwm2m_coap_start();
    zassert_equal(rc, 0, "transport start failed: %d", rc);
}

ZTEST_SUITE(sirena_lwm2m_protocol, NULL, suite_setup, test_before, NULL, NULL);

ZTEST(sirena_lwm2m_protocol, test_read_returns_the_resource_value)
{
    int64_t value;
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    rc = peer_request(COAP_METHOD_GET, "/1/0/1", 0U, NULL, 0U, NULL, 0U, -1, NULL);
    zassert_equal(rc, 0, "read request failed: %d", rc);

    zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_CONTENT, "unexpected response code");
    zassert_equal(peer_capture.content_format, SIRENA_LWM2M_CF_TLV,
                  "the response must use the preferred content format");

    value = peer_decode_integer(NULL);
    zassert_equal(value, PEER_LIFETIME, "unexpected resource value");
}

ZTEST(sirena_lwm2m_protocol, test_read_of_an_unknown_resource_is_not_found)
{
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    rc = peer_request(COAP_METHOD_GET, "/3/0/99", 0U, NULL, 0U, NULL, 0U, -1, NULL);
    zassert_equal(rc, 0, "read request failed: %d", rc);

    zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_NOT_FOUND, "unexpected response code");
    zassert_equal(peer_capture.payload_length, 0U, "a failure must not carry a payload");
}

ZTEST(sirena_lwm2m_protocol, test_path_with_too_many_segments_is_refused)
{
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    rc = peer_request(COAP_METHOD_GET, "/1/0/1/0", 0U, NULL, 0U, NULL, 0U, -1, NULL);
    zassert_equal(rc, 0, "read request failed: %d", rc);

    zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_NOT_FOUND, "unexpected response code");
    zassert_equal(peer_capture.payload_length, 0U, "a failure must not carry a payload");

    rc = peer_request(COAP_METHOD_GET, "/1/0/1", 0U, NULL, 0U, NULL, 0U, -1, NULL);
    zassert_equal(rc, 0, "the client did not survive the refused path: %d", rc);

    zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_CONTENT, "unexpected response code");
}

ZTEST(sirena_lwm2m_protocol, test_path_parser_stays_inside_the_destination)
{
    struct guarded_path guarded = {
        .path = { 0xAAAAU, 0xBBBBU, 0xCCCCU },
        .guard = { 0xDDDDU, 0xEEEEU, 0xFFFFU },
    };
    struct coap_packet request;
    uint8_t buffer[128];
    uint8_t token[COAP_TOKEN_MAX_LEN];
    size_t count = 0U;
    int rc;

    rc = coap_packet_init(&request, buffer, sizeof(buffer), COAP_VERSION_1, COAP_TYPE_CON,
                           sizeof(token), token, COAP_METHOD_GET, 1U);
    zassert_equal(rc, 0, "the request could not be built: %d", rc);

    rc = coap_packet_set_path(&request, "/1/0/1/0");
    zassert_equal(rc, 0, "the path could not be set: %d", rc);

    rc = sirena_lwm2m_coap_endpoint_parse_path(&request, guarded.path,
                                                ARRAY_SIZE(guarded.path), &count);
    zassert_true(rc != 0, "a path with too many segments must be refused");
    zassert_equal(count, 0U, "a refused path must not report a segment count");
    zassert_equal(guarded.guard[0], 0xDDDDU, "the parser wrote past the destination");
    zassert_equal(guarded.guard[1], 0xEEEEU, "the parser wrote past the destination");
    zassert_equal(guarded.guard[2], 0xFFFFU, "the parser wrote past the destination");
}

ZTEST(sirena_lwm2m_protocol, test_read_of_a_resource_that_cannot_be_read)
{
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    rc = peer_request(COAP_METHOD_GET, "/1/0/8", 0U, NULL, 0U, NULL, 0U, -1, NULL);
    zassert_equal(rc, 0, "read request failed: %d", rc);

    zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_NOT_ALLOWED, "unexpected response code");
    zassert_equal(peer_capture.payload_length, 0U, "a failure must not carry a payload");
}

ZTEST(sirena_lwm2m_protocol, test_write_updates_the_resource)
{
    uint8_t payload[PEER_PAYLOAD_MAX];
    int64_t lifetime = 0;
    size_t payload_length;
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    payload_length = peer_encode_integer(PEER_SERVER_OBJECT_ID, PEER_SERVER_LIFETIME_RESOURCE,
                                         PEER_LIFETIME_UPDATED, payload, sizeof(payload));

    rc = peer_request(COAP_METHOD_PUT, "/1/0/1", SIRENA_LWM2M_CF_TLV, payload, payload_length,
                      NULL, 0U, -1, NULL);
    zassert_equal(rc, 0, "write request failed: %d", rc);

    zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_CHANGED, "unexpected response code");

    rc = sirena_lwm2m_get_integer(PEER_SERVER_OBJECT_ID, PEER_INSTANCE_ID,
                                   PEER_SERVER_LIFETIME_RESOURCE, &lifetime);
    zassert_equal(rc, 0, "the stored value is not readable");
    zassert_equal(lifetime, PEER_LIFETIME_UPDATED, "the stored value was not updated");
}

ZTEST(sirena_lwm2m_protocol, test_write_to_a_read_only_resource_is_refused)
{
    uint8_t payload[PEER_PAYLOAD_MAX];
    char uri[64];
    size_t uri_length = sizeof(uri);
    size_t payload_length;
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    payload_length = peer_encode_integer(PEER_SECURITY_OBJECT_ID, PEER_SERVER_URI_RESOURCE, 1,
                                         payload, sizeof(payload));

    rc = peer_request(COAP_METHOD_PUT, "/0/0/0", SIRENA_LWM2M_CF_TLV, payload, payload_length,
                      NULL, 0U, -1, NULL);
    zassert_equal(rc, 0, "write request failed: %d", rc);

    zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_NOT_ALLOWED, "unexpected response code");

    rc = sirena_lwm2m_get_string(PEER_SECURITY_OBJECT_ID, PEER_INSTANCE_ID,
                                  PEER_SERVER_URI_RESOURCE, uri, &uri_length);
    zassert_equal(rc, 0, "the stored value is not readable");
    zassert_str_equal(uri, server_uri, "the stored value was changed");
}

ZTEST(sirena_lwm2m_protocol, test_write_with_an_undecodable_content_format_is_refused)
{
    static const uint8_t payload[] = { '{"bn":"/1/0/"}' };
    int64_t lifetime = 0;
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    rc = peer_request(COAP_METHOD_PUT, "/1/0/1", SIRENA_LWM2M_CF_SENML_JSON, payload,
                      sizeof(payload), NULL, 0U, -1, NULL);
    zassert_equal(rc, 0, "write request failed: %d", rc);

    zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT,
                  "unexpected response code");

    rc = sirena_lwm2m_get_integer(PEER_SERVER_OBJECT_ID, PEER_INSTANCE_ID,
                                   PEER_SERVER_LIFETIME_RESOURCE, &lifetime);
    zassert_equal(rc, 0, "the stored value is not readable");
    zassert_equal(lifetime, PEER_LIFETIME, "the stored value was changed");
}

ZTEST(sirena_lwm2m_protocol, test_execute_invokes_the_handler)
{
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    rc = peer_request(COAP_METHOD_POST, "/3/0/4", 0U, NULL, 0U, NULL, 0U, -1, NULL);
    zassert_equal(rc, 0, "execute request failed: %d", rc);

    zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_CHANGED, "unexpected response code");
    zassert_true(peer_execute_called, "the execute handler was not called");
    zassert_equal(peer_execute_object_id, PEER_DEVICE_OBJECT_ID, "unexpected object id");
    zassert_equal(peer_execute_resource_id, PEER_DEVICE_REBOOT_RESOURCE, "unexpected resource id");
}

ZTEST(sirena_lwm2m_protocol, test_execute_of_a_resource_that_cannot_be_executed)
{
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    rc = peer_request(COAP_METHOD_POST, "/1/0/1", 0U, NULL, 0U, NULL, 0U, -1, NULL);
    zassert_equal(rc, 0, "execute request failed: %d", rc);

    zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_NOT_ALLOWED, "unexpected response code");
    zassert_false(peer_execute_called, "the execute handler must not be called");
}

ZTEST(sirena_lwm2m_protocol, test_negotiation_uses_an_acceptable_format)
{
    static const uint16_t accepted[] = { SIRENA_LWM2M_CF_JSON };
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    rc = peer_request(COAP_METHOD_GET, "/1/0/1", 0U, NULL, 0U, accepted, ARRAY_SIZE(accepted),
                      -1, NULL);
    zassert_equal(rc, 0, "read request failed: %d", rc);

    zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_CONTENT, "unexpected response code");
    zassert_equal(peer_capture.content_format, SIRENA_LWM2M_CF_JSON,
                  "the response must use the acceptable content format");
    zassert_true(peer_capture.payload_length > 0U, "the response must carry a payload");
    zassert_not_null(strstr((const char *)peer_capture.payload, "300"),
                     "the payload must carry the resource value");
}

ZTEST(sirena_lwm2m_protocol, test_negotiation_refuses_an_unsupported_format)
{
    static const uint16_t accepted[] = { 12345U };
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    rc = peer_request(COAP_METHOD_GET, "/1/0/1", 0U, NULL, 0U, accepted, ARRAY_SIZE(accepted),
                      -1, NULL);
    zassert_equal(rc, 0, "read request failed: %d", rc);

    zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_NOT_ACCEPTABLE,
                  "unexpected response code");
    zassert_equal(peer_capture.payload_length, 0U, "a failure must not carry a payload");
}

ZTEST(sirena_lwm2m_protocol, test_negotiation_refuses_a_format_the_codec_cannot_encode)
{
    static const uint16_t accepted[] = { SIRENA_LWM2M_CF_PLAIN_TEXT };
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    rc = peer_request(COAP_METHOD_GET, "/1/0/1", 0U, NULL, 0U, accepted, ARRAY_SIZE(accepted),
                      -1, NULL);
    zassert_equal(rc, 0, "read request failed: %d", rc);

    zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_NOT_ACCEPTABLE,
                  "a format the codec cannot encode for the resource must be refused");
    zassert_equal(peer_capture.payload_length, 0U, "a failure must not carry a payload");
}

ZTEST(sirena_lwm2m_protocol, test_negotiation_uses_the_plain_text_format_for_a_string)
{
    static const uint16_t accepted[] = { SIRENA_LWM2M_CF_PLAIN_TEXT };
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    rc = peer_request(COAP_METHOD_GET, "/1/0/7", 0U, NULL, 0U, accepted, ARRAY_SIZE(accepted),
                      -1, NULL);
    zassert_equal(rc, 0, "read request failed: %d", rc);

    zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_CONTENT, "unexpected response code");
    zassert_equal(peer_capture.content_format, SIRENA_LWM2M_CF_PLAIN_TEXT,
                  "the response must carry the content format of the representation");
    zassert_true(peer_capture.payload_length > 0U, "the response must carry a payload");
    zassert_equal(memcmp(peer_capture.payload, "U", 1U), 0,
                  "the payload must carry the resource value");
}

ZTEST(sirena_lwm2m_protocol, test_negotiation_refuses_an_accept_list_it_cannot_hold)
{
    static const uint16_t accepted[] = {
        SIRENA_LWM2M_CF_TLV, SIRENA_LWM2M_CF_JSON, SIRENA_LWM2M_CF_CBOR,
        SIRENA_LWM2M_CF_SENML_JSON, SIRENA_LWM2M_CF_SENML_CBOR,
    };
    int rc;

    zassert_true(ARRAY_SIZE(accepted) > CONFIG_SIRENA_LWM2M_ACCEPTED_FORMATS_MAX,
                 "the test must exceed the accepted format capacity");

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    rc = peer_request(COAP_METHOD_GET, "/1/0/1", 0U, NULL, 0U, accepted, ARRAY_SIZE(accepted),
                      -1, NULL);
    zassert_equal(rc, 0, "read request failed: %d", rc);

    zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_NOT_ACCEPTABLE,
                  "an accept list the client cannot hold must be refused");
}

ZTEST(sirena_lwm2m_protocol, test_write_does_not_negotiate_on_the_acceptable_formats)
{
    static const uint16_t accepted[] = { 12345U };
    uint8_t payload[PEER_PAYLOAD_MAX];
    int64_t lifetime = 0;
    size_t payload_length;
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    payload_length = peer_encode_integer(PEER_SERVER_OBJECT_ID, PEER_SERVER_LIFETIME_RESOURCE,
                                         PEER_LIFETIME_UPDATED, payload, sizeof(payload));

    rc = peer_request(COAP_METHOD_PUT, "/1/0/1", SIRENA_LWM2M_CF_TLV, payload, payload_length,
                      accepted, ARRAY_SIZE(accepted), -1, NULL);
    zassert_equal(rc, 0, "write request failed: %d", rc);

    zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_CHANGED, "unexpected response code");

    rc = sirena_lwm2m_get_integer(PEER_SERVER_OBJECT_ID, PEER_INSTANCE_ID,
                                   PEER_SERVER_LIFETIME_RESOURCE, &lifetime);
    zassert_equal(rc, 0, "the stored value is not readable");
    zassert_equal(lifetime, PEER_LIFETIME_UPDATED, "the stored value was not updated");
}

ZTEST(sirena_lwm2m_protocol, test_negotiation_without_accept_uses_the_preferred_format)
{
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    rc = peer_request(COAP_METHOD_GET, "/1/0/1", 0U, NULL, 0U, NULL, 0U, -1, NULL);
    zassert_equal(rc, 0, "read request failed: %d", rc);

    zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_CONTENT, "unexpected response code");
    zassert_equal(peer_capture.content_format, CONFIG_SIRENA_LWM2M_PREFERRED_CONTENT_FORMAT,
                  "the response must use the preferred content format");
}

ZTEST(sirena_lwm2m_protocol, test_observe_registers_and_notifies)
{
    struct coap_client_request observe_request;
    uint8_t token[COAP_TOKEN_MAX_LEN];
    uint8_t token_length;
    int64_t value;
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    rc = peer_request(COAP_METHOD_GET, "/1/0/1", 0U, NULL, 0U, NULL, 0U, 0, &observe_request);
    zassert_equal(rc, 0, "observe request failed: %d", rc);

    zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_CONTENT, "unexpected response code");
    zassert_true(peer_capture.observe >= 0, "the response must carry the observe option");

    token_length = peer_capture.token_length;
    memcpy(token, peer_capture.token, token_length);

    value = peer_decode_integer(NULL);
    zassert_equal(value, PEER_LIFETIME, "unexpected resource value");

    rc = sirena_lwm2m_set_integer(PEER_SERVER_OBJECT_ID, PEER_INSTANCE_ID,
                                   PEER_SERVER_LIFETIME_RESOURCE, PEER_LIFETIME_NOTIFIED);
    zassert_equal(rc, 0, "the value update failed");

    rc = sirena_lwm2m_notify_resource(PEER_SERVER_OBJECT_ID, PEER_INSTANCE_ID,
                                      PEER_SERVER_LIFETIME_RESOURCE);
    zassert_equal(rc, 1, "the notification must reach the observation: %d", rc);

    rc = k_sem_take(&peer_semaphore, K_SECONDS(10));
    zassert_equal(rc, 0, "no notification was received");

    zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_CONTENT, "unexpected notification code");
    zassert_equal(peer_capture.type, COAP_TYPE_CON,
                  "the notification must be a confirmable message");
    zassert_equal(peer_capture.token_length, token_length, "the notification token length differs");
    zassert_equal(memcmp(peer_capture.token, token, token_length), 0,
                  "the notification must reuse the observation token");

    value = peer_decode_integer(NULL);
    zassert_equal(value, PEER_LIFETIME_NOTIFIED, "the notification carries a stale value");
}

ZTEST(sirena_lwm2m_protocol, test_observation_capacity_is_bounded)
{
    size_t i;
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    for (i = 0U; i < CONFIG_SIRENA_LWM2M_MAX_OBSERVATIONS; ++i) {
        rc = peer_request(COAP_METHOD_GET, "/1/0/1", 0U, NULL, 0U, NULL, 0U, 0, NULL);
        zassert_equal(rc, 0, "observe request failed: %d", rc);
        zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_CONTENT, "unexpected response code");
    }

    rc = peer_request(COAP_METHOD_GET, "/1/0/1", 0U, NULL, 0U, NULL, 0U, 0, NULL);
    zassert_equal(rc, 0, "observe request failed: %d", rc);
    zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_SERVICE_UNAVAILABLE,
                  "an observation beyond the capacity must be refused");
}

ZTEST(sirena_lwm2m_protocol, test_cancelled_observation_stops_the_notifications)
{
    struct coap_client_request observe_request;
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    rc = peer_request(COAP_METHOD_GET, "/1/0/1", 0U, NULL, 0U, NULL, 0U, 0, &observe_request);
    zassert_equal(rc, 0, "observe request failed: %d", rc);
    zassert_equal(peer_capture.code, COAP_RESPONSE_CODE_CONTENT, "unexpected response code");

    rc = coap_client_deregister_observe(&peer_client, &observe_request);
    zassert_equal(rc, 0, "the observation could not be cancelled: %d", rc);

    rc = k_sem_take(&peer_semaphore, K_SECONDS(10));
    zassert_equal(rc, 0, "no answer to the cancellation was received");

    rc = sirena_lwm2m_set_integer(PEER_SERVER_OBJECT_ID, PEER_INSTANCE_ID,
                                   PEER_SERVER_LIFETIME_RESOURCE, PEER_LIFETIME_NOTIFIED);
    zassert_equal(rc, 0, "the value update failed");

    rc = sirena_lwm2m_notify_resource(PEER_SERVER_OBJECT_ID, PEER_INSTANCE_ID,
                                      PEER_SERVER_LIFETIME_RESOURCE);
    zassert_equal(rc, 0, "a cancelled observation must not be notified: %d", rc);

    rc = k_sem_take(&peer_semaphore, K_MSEC(500));
    zassert_not_equal(rc, 0, "a cancelled observation must not be notified");
}
