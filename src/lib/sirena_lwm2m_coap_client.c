#include "sirena_lwm2m_coap_client.h"
#include <errno.h>

static struct coap_client client;
static bool client_ready;

int sirena_lwm2m_coap_client_get(struct coap_client **result)
{
    int rc;

    if (result == NULL) {
        return -EINVAL;
    }

    if (!client_ready) {
        rc = coap_client_init(&client, "sirena_lwm2m");
        if (rc != 0) {
            return rc;
        }

        client_ready = true;
    }

    *result = &client;

    return 0;
}
