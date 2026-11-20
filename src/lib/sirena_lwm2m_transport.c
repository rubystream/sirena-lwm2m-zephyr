#include <sirena/lwm2m/transport.h>
#include <sirena/lwm2m/engine.h>
#include <zephyr/kernel.h>
#include <errno.h>

static const struct sirena_lwm2m_transport_ops *transport_ops;

static int transport_dispatch(enum sirena_lwm2m_engine_operation operation)
{
    if (transport_ops == NULL) {
        return -ENOTSUP;
    }

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

int sirena_lwm2m_transport_bind(const struct sirena_lwm2m_transport_ops *ops)
{
    if (ops == NULL) {
        return -EINVAL;
    }

    transport_ops = ops;

    return sirena_lwm2m_engine_start(transport_dispatch);
}

int sirena_lwm2m_transport_register(void)
{
    return sirena_lwm2m_engine_submit(SIRENA_LWM2M_ENGINE_OPERATION_REGISTER);
}

int sirena_lwm2m_transport_update(void)
{
    return sirena_lwm2m_engine_submit(SIRENA_LWM2M_ENGINE_OPERATION_UPDATE);
}

int sirena_lwm2m_transport_deregister(void)
{
    return sirena_lwm2m_engine_submit(SIRENA_LWM2M_ENGINE_OPERATION_DEREGISTER);
}
