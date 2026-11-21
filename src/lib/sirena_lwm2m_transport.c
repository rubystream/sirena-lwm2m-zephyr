#include "sirena_lwm2m_bindings.h"
#include "sirena_lwm2m_uri.h"
#include <sirena/lwm2m/engine.h>
#include <sirena/lwm2m/objects.h>
#include <sirena/lwm2m/protocol.h>
#include <sirena/lwm2m/transport.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <string.h>

#define SIRENA_LWM2M_SECURITY_OBJECT_ID 0U
#define SIRENA_LWM2M_SERVER_URI_RESOURCE 0U
#define SIRENA_LWM2M_SECURITY_MODE_RESOURCE 2U

struct sirena_lwm2m_binding_entry {
    enum sirena_lwm2m_transport_binding binding;
    const struct sirena_lwm2m_transport_ops *ops;
    int (*start)(void);
    int (*stop)(void);
};

static const struct sirena_lwm2m_binding_entry binding_entries[] = {
    {
        .binding = SIRENA_LWM2M_TRANSPORT_BINDING_NONE,
    },
#if IS_ENABLED(CONFIG_SIRENA_LWM2M_TRANSPORT_COAP)
    {
        .binding = SIRENA_LWM2M_TRANSPORT_BINDING_COAP,
        .ops = &sirena_lwm2m_coap_transport_ops,
        .start = sirena_lwm2m_coap_start_engine,
        .stop = sirena_lwm2m_coap_stop_engine,
    },
#endif
#if IS_ENABLED(CONFIG_SIRENA_LWM2M_TRANSPORT_DTLS)
    {
        .binding = SIRENA_LWM2M_TRANSPORT_BINDING_DTLS,
        .ops = &sirena_lwm2m_dtls_transport_ops,
        .start = sirena_lwm2m_dtls_start_engine,
        .stop = sirena_lwm2m_dtls_stop_engine,
    },
#endif
#if IS_ENABLED(CONFIG_SIRENA_LWM2M_TRANSPORT_MQTT)
    {
        .binding = SIRENA_LWM2M_TRANSPORT_BINDING_MQTT,
        .ops = &sirena_lwm2m_mqtt_transport_ops,
        .start = sirena_lwm2m_mqtt_start_engine,
        .stop = sirena_lwm2m_mqtt_stop_engine,
    },
#endif
};

static struct k_mutex transport_lock;
static const struct sirena_lwm2m_transport_ops *transport_ops;
static const struct sirena_lwm2m_transport_ops *pending_ops;
static enum sirena_lwm2m_transport_binding transport_binding;
static char endpoint_name[CONFIG_SIRENA_LWM2M_ENDPOINT_NAME_MAX + 1U];

static int transport_poll(int timeout_ms);

static const struct sirena_lwm2m_binding_entry *binding_entry(
    enum sirena_lwm2m_transport_binding binding)
{
    size_t i;

    for (i = 0U; i < ARRAY_SIZE(binding_entries); ++i) {
        if (binding_entries[i].binding == binding) {
            return &binding_entries[i];
        }
    }

    return NULL;
}

static enum sirena_lwm2m_transport_binding binding_of(
    const struct sirena_lwm2m_transport_ops *ops)
{
    size_t i;

    for (i = 0U; i < ARRAY_SIZE(binding_entries); ++i) {
        if (binding_entries[i].ops != NULL && binding_entries[i].ops == ops) {
            return binding_entries[i].binding;
        }
    }

    return SIRENA_LWM2M_TRANSPORT_BINDING_NONE;
}

static int transport_stop_binding(enum sirena_lwm2m_transport_binding binding)
{
    const struct sirena_lwm2m_binding_entry *entry = binding_entry(binding);
    int rc = 0;

    if (entry != NULL && entry->ops != NULL) {
        rc = entry->stop();
    }

    if (transport_binding == binding) {
        transport_ops = NULL;
        transport_binding = SIRENA_LWM2M_TRANSPORT_BINDING_NONE;
        (void)sirena_lwm2m_engine_set_poll_fn(NULL);
    }

    return rc;
}

static int transport_start_binding(enum sirena_lwm2m_transport_binding binding)
{
    const struct sirena_lwm2m_binding_entry *entry = binding_entry(binding);
    bool switched = transport_binding != binding;
    int rc;

    if (entry == NULL || entry->ops == NULL) {
        return -ENOTSUP;
    }

    if (switched) {
        rc = transport_stop_binding(transport_binding);
        if (rc != 0) {
            return rc;
        }
    }

    transport_ops = entry->ops;
    transport_binding = binding;

    (void)sirena_lwm2m_engine_set_poll_fn(transport_poll);

    rc = entry->start();
    if (rc != 0 && switched) {
        transport_ops = NULL;
        transport_binding = SIRENA_LWM2M_TRANSPORT_BINDING_NONE;
        (void)sirena_lwm2m_engine_set_poll_fn(NULL);
    }

    return rc;
}

static int transport_binding_for(const char *uri, int64_t mode,
                                 enum sirena_lwm2m_transport_binding *binding)
{
    enum sirena_lwm2m_uri_scheme scheme;
    int rc;

    rc = sirena_lwm2m_uri_scheme(uri, &scheme);
    if (rc != 0) {
        return rc;
    }

    switch (mode) {
    case SIRENA_LWM2M_SECURITY_MODE_PSK:
        switch (scheme) {
        case SIRENA_LWM2M_URI_SCHEME_COAPS:
            *binding = SIRENA_LWM2M_TRANSPORT_BINDING_DTLS;
            return 0;
        case SIRENA_LWM2M_URI_SCHEME_MQTTS:
            *binding = SIRENA_LWM2M_TRANSPORT_BINDING_MQTT;
            return 0;
        default:
            return -EINVAL;
        }
    case SIRENA_LWM2M_SECURITY_MODE_NOSEC:
        if (!IS_ENABLED(CONFIG_SIRENA_LWM2M_TRANSPORT_NOSEC)) {
            return -EPERM;
        }
        switch (scheme) {
        case SIRENA_LWM2M_URI_SCHEME_COAP:
            *binding = SIRENA_LWM2M_TRANSPORT_BINDING_COAP;
            return 0;
        case SIRENA_LWM2M_URI_SCHEME_MQTT:
            *binding = SIRENA_LWM2M_TRANSPORT_BINDING_MQTT;
            return 0;
        default:
            return -EINVAL;
        }
    default:
        return -ENOTSUP;
    }
}

static int transport_start_selected(void)
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

    return transport_start_binding(binding);
}

static int transport_bind_pending(void)
{
    const struct sirena_lwm2m_transport_ops *ops;

    k_mutex_lock(&transport_lock, K_FOREVER);
    ops = pending_ops;
    pending_ops = NULL;
    k_mutex_unlock(&transport_lock);

    if (ops == NULL) {
        return -EINVAL;
    }

    if (transport_ops != NULL && transport_ops != ops) {
        return -EBUSY;
    }

    transport_ops = ops;
    transport_binding = binding_of(ops);

    return 0;
}

static int transport_poll(int timeout_ms)
{
    if (transport_ops == NULL || transport_ops->poll == NULL) {
        return 0;
    }

    return transport_ops->poll(timeout_ms);
}

static int transport_dispatch(enum sirena_lwm2m_engine_operation operation, void *payload)
{
    switch (operation) {
    case SIRENA_LWM2M_ENGINE_OPERATION_START:
        return transport_start_selected();
    case SIRENA_LWM2M_ENGINE_OPERATION_START_COAP:
        return transport_start_binding(SIRENA_LWM2M_TRANSPORT_BINDING_COAP);
    case SIRENA_LWM2M_ENGINE_OPERATION_START_DTLS:
        return transport_start_binding(SIRENA_LWM2M_TRANSPORT_BINDING_DTLS);
    case SIRENA_LWM2M_ENGINE_OPERATION_STOP_COAP:
        return transport_stop_binding(SIRENA_LWM2M_TRANSPORT_BINDING_COAP);
    case SIRENA_LWM2M_ENGINE_OPERATION_STOP_DTLS:
        return transport_stop_binding(SIRENA_LWM2M_TRANSPORT_BINDING_DTLS);
    case SIRENA_LWM2M_ENGINE_OPERATION_START_MQTT:
        return transport_start_binding(SIRENA_LWM2M_TRANSPORT_BINDING_MQTT);
    case SIRENA_LWM2M_ENGINE_OPERATION_STOP_MQTT:
        return transport_stop_binding(SIRENA_LWM2M_TRANSPORT_BINDING_MQTT);
    case SIRENA_LWM2M_ENGINE_OPERATION_REGISTER:
        return transport_ops != NULL ? transport_ops->register_client() : -ENOTSUP;
    case SIRENA_LWM2M_ENGINE_OPERATION_UPDATE:
        return transport_ops != NULL ? transport_ops->update() : -ENOTSUP;
    case SIRENA_LWM2M_ENGINE_OPERATION_DEREGISTER:
        return transport_ops != NULL ? transport_ops->deregister() : -ENOTSUP;
    case SIRENA_LWM2M_ENGINE_OPERATION_BIND:
        return transport_bind_pending();
    case SIRENA_LWM2M_ENGINE_OPERATION_NOTIFY: {
        struct sirena_lwm2m_notify_request *request = payload;

        if (request == NULL || transport_ops == NULL || transport_ops->notify == NULL) {
            return -ENOTSUP;
        }

        return transport_ops->notify(request->object_id, request->instance_id,
                                     request->resource_id);
    }
    default:
        return -EINVAL;
    }
}

int sirena_lwm2m_transport_engine_ensure(void)
{
    int rc;

    rc = sirena_lwm2m_engine_start(transport_dispatch);
    if (rc == -EALREADY) {
        rc = 0;
    }

    return rc;
}

int sirena_lwm2m_transport_bind(const struct sirena_lwm2m_transport_ops *ops)
{
    int rc;

    if (ops == NULL) {
        return -EINVAL;
    }

    rc = sirena_lwm2m_transport_engine_ensure();
    if (rc != 0) {
        return rc;
    }

    k_mutex_lock(&transport_lock, K_FOREVER);
    pending_ops = ops;
    k_mutex_unlock(&transport_lock);

    return sirena_lwm2m_engine_submit(SIRENA_LWM2M_ENGINE_OPERATION_BIND, NULL);
}

static int transport_submit(enum sirena_lwm2m_engine_operation operation)
{
    int rc;

    rc = sirena_lwm2m_transport_engine_ensure();
    if (rc != 0) {
        return rc;
    }

    return sirena_lwm2m_engine_submit(operation, NULL);
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
    return transport_submit(SIRENA_LWM2M_ENGINE_OPERATION_START);
}
