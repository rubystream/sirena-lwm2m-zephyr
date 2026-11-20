#include <sirena/lwm2m/transport.h>
#include <sirena/lwm2m/engine.h>
#include <sirena/lwm2m/objects.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <string.h>

#if IS_ENABLED(CONFIG_SIRENA_LWM2M_TRANSPORT_COAP)
#include <sirena/lwm2m/coap.h>
#endif

#if IS_ENABLED(CONFIG_SIRENA_LWM2M_TRANSPORT_DTLS)
#include <sirena/lwm2m/dtls.h>
#endif

#define SIRENA_LWM2M_SECURITY_OBJECT_ID 0U
#define SIRENA_LWM2M_SERVER_URI_RESOURCE 0U
#define SIRENA_LWM2M_SECURITY_MODE_RESOURCE 2U

#define SIRENA_LWM2M_COAP_SCHEME "coap://"
#define SIRENA_LWM2M_COAPS_SCHEME "coaps://"

enum sirena_lwm2m_transport_binding {
    SIRENA_LWM2M_TRANSPORT_BINDING_NONE,
    SIRENA_LWM2M_TRANSPORT_BINDING_COAP,
    SIRENA_LWM2M_TRANSPORT_BINDING_DTLS,
};

static const struct sirena_lwm2m_transport_ops *transport_ops;
static char endpoint_name[CONFIG_SIRENA_LWM2M_ENDPOINT_NAME_MAX + 1U];

static int transport_dispatch(enum sirena_lwm2m_engine_operation operation)
{
    switch (operation) {
    case SIRENA_LWM2M_ENGINE_OPERATION_REGISTER:
        return transport_ops->register_client();
    case SIRENA_LWM2M_ENGINE_OPERATION_UPDATE:
        return transport_ops->update();
    case SIRENA_LWM2M_ENGINE_OPERATION_DEREGISTER:
        return transport_ops->deregister();
    default:
        return -EINVAL;
    }
}

static int transport_submit(enum sirena_lwm2m_engine_operation operation)
{
    if (transport_ops == NULL) {
        return -ENOTSUP;
    }

    return sirena_lwm2m_engine_submit(operation);
}

static bool transport_scheme_is(const char *uri, const char *scheme)
{
    return strncmp(uri, scheme, strlen(scheme)) == 0;
}

static int transport_binding_for(const char *uri, int64_t mode,
                                 enum sirena_lwm2m_transport_binding *binding)
{
    switch (mode) {
    case SIRENA_LWM2M_SECURITY_MODE_PSK:
        if (!transport_scheme_is(uri, SIRENA_LWM2M_COAPS_SCHEME)) {
            return -EINVAL;
        }
        *binding = SIRENA_LWM2M_TRANSPORT_BINDING_DTLS;
        return 0;
    case SIRENA_LWM2M_SECURITY_MODE_NOSEC:
        if (!IS_ENABLED(CONFIG_SIRENA_LWM2M_TRANSPORT_NOSEC)) {
            return -EPERM;
        }
        if (!transport_scheme_is(uri, SIRENA_LWM2M_COAP_SCHEME)) {
            return -EINVAL;
        }
        *binding = SIRENA_LWM2M_TRANSPORT_BINDING_COAP;
        return 0;
    default:
        return -EPROTONOSUPPORT;
    }
}

int sirena_lwm2m_transport_bind(const struct sirena_lwm2m_transport_ops *ops)
{
    const struct sirena_lwm2m_transport_ops *previous;
    int rc;

    if (ops == NULL) {
        return -EINVAL;
    }

    previous = transport_ops;
    transport_ops = ops;

    rc = sirena_lwm2m_engine_start(transport_dispatch);
    if (rc == -EALREADY) {
        rc = 0;
    }

    if (rc != 0) {
        transport_ops = previous;
    }

    return rc;
}

int sirena_lwm2m_transport_register(void)
{
    return transport_submit(SIRENA_LWM2M_ENGINE_OPERATION_REGISTER);
}

int sirena_lwm2m_transport_update(void)
{
    return transport_submit(SIRENA_LWM2M_ENGINE_OPERATION_UPDATE);
}

int sirena_lwm2m_transport_deregister(void)
{
    return transport_submit(SIRENA_LWM2M_ENGINE_OPERATION_DEREGISTER);
}

int sirena_lwm2m_transport_set_endpoint_name(const char *name)
{
    size_t length;

    if (name == NULL) {
        return -EINVAL;
    }

    length = strlen(name);
    if (length > CONFIG_SIRENA_LWM2M_ENDPOINT_NAME_MAX) {
        return -EINVAL;
    }

    memcpy(endpoint_name, name, length + 1U);

    return 0;
}

const char *sirena_lwm2m_transport_endpoint_name(void)
{
    return endpoint_name;
}

int sirena_lwm2m_transport_start(void)
{
    enum sirena_lwm2m_transport_binding binding = SIRENA_LWM2M_TRANSPORT_BINDING_NONE;
    char uri[CONFIG_SIRENA_LWM2M_SERVER_URI_MAX + 1U];
    size_t length = sizeof(uri);
    int64_t mode;
    int rc;

    rc = sirena_lwm2m_get_string(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                 SIRENA_LWM2M_SERVER_URI_RESOURCE, uri, &length);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_get_integer(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                  SIRENA_LWM2M_SECURITY_MODE_RESOURCE, &mode);
    if (rc != 0) {
        return rc;
    }

    rc = transport_binding_for(uri, mode, &binding);
    if (rc != 0) {
        return rc;
    }

    switch (binding) {
    case SIRENA_LWM2M_TRANSPORT_BINDING_DTLS:
#if IS_ENABLED(CONFIG_SIRENA_LWM2M_TRANSPORT_DTLS)
#if IS_ENABLED(CONFIG_SIRENA_LWM2M_TRANSPORT_COAP)
        (void)sirena_lwm2m_coap_stop();
#endif
        return sirena_lwm2m_dtls_start();
#else
        return -ENOTSUP;
#endif
    case SIRENA_LWM2M_TRANSPORT_BINDING_COAP:
#if IS_ENABLED(CONFIG_SIRENA_LWM2M_TRANSPORT_COAP)
#if IS_ENABLED(CONFIG_SIRENA_LWM2M_TRANSPORT_DTLS)
        (void)sirena_lwm2m_dtls_stop();
#endif
        return sirena_lwm2m_coap_start();
#else
        return -ENOTSUP;
#endif
    default:
        return -EINVAL;
    }
}
