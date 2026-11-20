#include <sirena/lwm2m/transport.h>
#include <sirena/lwm2m/engine.h>
#include <zephyr/kernel.h>
#include <errno.h>

static const struct sirena_lwm2m_transport_ops *transport_ops;

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
