#include <zephyr/ztest.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/coap_client.h>
#include <zephyr/net/coap_service.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/util.h>
#include <sirena/lwm2m/codec.h>
#include <sirena/lwm2m/coap.h>
#include <sirena/lwm2m/object_registry.h>
#include <sirena/lwm2m/objects.h>
#include <sirena/lwm2m/protocol.h>
#include <sirena/lwm2m/transport.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define BOUNDS_ENDPOINT_NAME "sirena-protocol-bounds-test"
#define BOUNDS_SERVER_OBJECT_ID 1U
#define BOUNDS_DEVICE_OBJECT_ID 3U
#define BOUNDS_INSTANCE_ID 0U
#define BOUNDS_LIFETIME_RESOURCE 1U
#define BOUNDS_MANUFACTURER_RESOURCE 0U
#define BOUNDS_LOCATION "rd"
#define BOUNDS_LOCATION_ID "1"
#define BOUNDS_PAYLOAD_MAX 128

struct bounds_capture {
    int code;
    int content_format;
    uint8_t payload[BOUNDS_PAYLOAD_MAX];
    size_t payload_length;
};

static struct bounds_capture bounds_capture;
static struct net_sockaddr_storage peer_client_address;
static net_socklen_t peer_client_address_length;
static bool peer_registered;
static uint16_t peer_port;
static char server_uri[64];
static struct coap_client peer_client;
static int peer_socket = -1;
static K_SEM_DEFINE(peer_semaphore, 0, 64);

static int peer_reply(struct coap_resource *resource, const struct coap_packet *request,
                      struct net_sockaddr *addr, net_socklen_t addr_len)
{
    static const char *const location[] = { BOUNDS_LOCATION, BOUNDS_LOCATION_ID, NULL };
    uint8_t buffer[64];
    uint8_t token[COAP_TOKEN_MAX_LEN];
    uint8_t token_length = coap_header_get_token(request, token);
    uint8_t type = coap_header_get_type(request) == COAP_TYPE_CON ? COAP_TYPE_ACK
                                                                    : COAP_TYPE_NON_CON;
    struct coap_packet response;
    int rc;
    int i;

    rc = coap_packet_init(&response, buffer, sizeof(buffer), COAP_VERSION_1, type, token_length,
                           token, COAP_RESPONSE_CODE_CREATED, coap_header_get_id(request));
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

static int peer_register_post(struct coap_resource *resource, struct coap_packet *request,
                              struct net_sockaddr *addr, net_socklen_t addr_len)
{
    static const char *const location[] = { BOUNDS_LOCATION, BOUNDS_LOCATION_ID, NULL };

    peer_registered = true;
    memcpy(&peer_client_address, addr, sizeof(peer_client_address));
    peer_client_address_length = addr_len;

    return peer_reply(resource, request, addr, addr_len);
}

COAP_SERVICE_DEFINE(peer_service, "127.0.0.1", &peer_port, 0);

static const char *const peer_register_path[] = { BOUNDS_LOCATION, NULL };
COAP_RESOURCE_DEFINE(peer_register_resource, peer_service, {
    .path = peer_register_path,
    .post = peer_register_post,
});

static void peer_response_handler(const struct coap_client_response_data *data, void *user_data)
{
    (void)user_data;

    bounds_capture.code = data->result_code;
    bounds_capture.content_format = -1;
    bounds_capture.payload_length = 0U;

    if (data->packet != NULL) {
        bounds_capture.content_format = coap_get_option_int(data->packet,
                                                              COAP_OPTION_CONTENT_FORMAT);
    }

    if (data->payload != NULL && data->payload_len < sizeof(bounds_capture.payload)) {
        memcpy(bounds_capture.payload, data->payload, data->payload_len);
        bounds_capture.payload_length = data->payload_len;
    }

    k_sem_give(&peer_semaphore);
}

static int peer_request(enum coap_method method, const char *path, uint16_t content_format,
                         const void *payload, size_t payload_length, const uint16_t *accept,
                         size_t accept_count)
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

    rc = coap_client_req(&peer_client, peer_socket, net_sad(&peer_client_address), &req, NULL);
    if (rc != 0) {
        return rc;
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

    rc = sirena_lwm2m_coap_set_endpoint_name(BOUNDS_ENDPOINT_NAME);
    zassert_equal(rc, 0, "endpoint name rejected");

    rc = sirena_lwm2m_transport_register();
    zassert_equal(rc, 0, "registration failed: %d", rc);
    zassert_true(peer_registered, "the peer received no registration");

    return 0;
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

    rc = coap_client_init(&peer_client, "sirena_lwm2m_bounds_test_peer");
    zassert_equal(rc, 0, "peer client init failed: %d", rc);

    return NULL;
}

static void test_before(void *fixture)
{
    struct sirena_lwm2m_provisioning provisioning = {
        .server_uri = server_uri,
        .security_mode = SIRENA_LWM2M_SECURITY_MODE_NOSEC,
        .short_server_id = 1U,
        .lifetime = 300U,
        .binding = "U",
    };
    int rc;

    (void)fixture;

    memset(&bounds_capture, 0, sizeof(bounds_capture));
    peer_registered = false;
    k_sem_reset(&peer_semaphore);

    rc = sirena_lwm2m_coap_stop();
    zassert_equal(rc, 0, "transport stop failed: %d", rc);

    rc = sirena_lwm2m_registry_init();
    zassert_equal(rc, 0, "registry init failed");

    rc = sirena_lwm2m_objects_init();
    zassert_equal(rc, 0, "objects init failed");

    rc = sirena_lwm2m_provision(&provisioning);
    zassert_equal(rc, 0, "provisioning failed: %d", rc);

    rc = sirena_lwm2m_coap_start();
    zassert_equal(rc, 0, "transport start failed: %d", rc);
}

ZTEST_SUITE(sirena_lwm2m_protocol_bounds, NULL, suite_setup, test_before, NULL, NULL);

ZTEST(sirena_lwm2m_protocol_bounds, test_read_composite_of_a_value_that_does_not_fit)
{
    static const char write_payload[] =
        "[{\"bn\":\"/1/0/\",\"n\":\"7\",\"v\":\"012345678901234567890123456789\"}]";
    static const char read_payload[] = "[{\"n\":\"/1/0/7\"}]";
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    rc = peer_request(COAP_METHOD_IPATCH, "/1/0", SIRENA_LWM2M_CF_SENML_JSON, write_payload,
                      sizeof(write_payload) - 1U, NULL, 0U);
    zassert_equal(rc, 0, "composite write request failed: %d", rc);
    zassert_equal(bounds_capture.code, COAP_RESPONSE_CODE_CHANGED,
                  "the value must be stored before the read");

    rc = peer_request(COAP_METHOD_FETCH, "/1/0", SIRENA_LWM2M_CF_SENML_JSON, read_payload,
                      sizeof(read_payload) - 1U, NULL, 0U);
    zassert_equal(rc, 0, "composite read request failed: %d", rc);

    zassert_equal(bounds_capture.code, COAP_RESPONSE_CODE_INTERNAL_ERROR,
                  "a value that does not fit must not be truncated");
    zassert_equal(bounds_capture.payload_length, 0U, "a failure must not carry a payload");
}

ZTEST(sirena_lwm2m_protocol_bounds, test_read_composite_whose_response_does_not_fit)
{
    static const char payload[] =
        "[{\"n\":\"/1/0/1\"},{\"n\":\"/1/0/7\"},{\"n\":\"/3/0/0\"}]";
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    rc = peer_request(COAP_METHOD_FETCH, "/1/0", SIRENA_LWM2M_CF_SENML_JSON, payload,
                      sizeof(payload) - 1U, NULL, 0U);
    zassert_equal(rc, 0, "composite read request failed: %d", rc);

    zassert_equal(bounds_capture.code, COAP_RESPONSE_CODE_INTERNAL_ERROR,
                  "a response that does not fit must not be truncated");
    zassert_equal(bounds_capture.payload_length, 0U, "a failure must not carry a payload");
}

ZTEST(sirena_lwm2m_protocol_bounds, test_write_composite_whose_response_does_not_fit)
{
    static const char payload[] =
        "[{\"bn\":\"/1/0/\",\"n\":\"1\",\"v\":600},{\"bn\":\"/1/0/\",\"n\":\"6\",\"v\":true},"
        "{\"bn\":\"/1/0/\",\"n\":\"7\",\"v\":\"udp\"}]";
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    rc = peer_request(COAP_METHOD_IPATCH, "/1/0", SIRENA_LWM2M_CF_SENML_JSON, payload,
                      sizeof(payload) - 1U, NULL, 0U);
    zassert_equal(rc, 0, "composite write request failed: %d", rc);

    zassert_equal(bounds_capture.code, COAP_RESPONSE_CODE_INTERNAL_ERROR,
                  "a response that does not fit must not be truncated");
    zassert_equal(bounds_capture.payload_length, 0U, "a failure must not carry a payload");
}

ZTEST(sirena_lwm2m_protocol_bounds, test_read_composite_refuses_an_oversized_accept_list)
{
    static const char payload[] = "[{\"n\":\"/1/0/1\"}]";
    const uint16_t accepted[] = { SIRENA_LWM2M_CF_SENML_JSON, SIRENA_LWM2M_CF_SENML_CBOR,
                                  SIRENA_LWM2M_CF_TLV };
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    rc = peer_request(COAP_METHOD_FETCH, "/1/0", SIRENA_LWM2M_CF_SENML_JSON, payload,
                      sizeof(payload) - 1U, accepted, ARRAY_SIZE(accepted));
    zassert_equal(rc, 0, "composite read request failed: %d", rc);

    zassert_equal(bounds_capture.code, COAP_RESPONSE_CODE_NOT_ACCEPTABLE,
                  "unexpected response code");
    zassert_equal(bounds_capture.payload_length, 0U, "a failure must not carry a payload");
}

ZTEST(sirena_lwm2m_protocol_bounds, test_write_composite_refuses_an_oversized_accept_list)
{
    static const char payload[] = "[{\"bn\":\"/1/0/\",\"n\":\"1\",\"v\":600}]";
    const uint16_t accepted[] = { SIRENA_LWM2M_CF_SENML_JSON, SIRENA_LWM2M_CF_SENML_CBOR,
                                  SIRENA_LWM2M_CF_TLV };
    int rc;

    rc = peer_register();
    zassert_equal(rc, 0, "registration failed");

    rc = peer_request(COAP_METHOD_IPATCH, "/1/0", SIRENA_LWM2M_CF_SENML_JSON, payload,
                      sizeof(payload) - 1U, accepted, ARRAY_SIZE(accepted));
    zassert_equal(rc, 0, "composite write request failed: %d", rc);

    zassert_equal(bounds_capture.code, COAP_RESPONSE_CODE_NOT_ACCEPTABLE,
                  "unexpected response code");
    zassert_equal(bounds_capture.payload_length, 0U, "a failure must not carry a payload");
}
