#include "sirena_lwm2m_credentials.h"
#include <sirena/lwm2m/objects.h>
#include <zephyr/sys/util.h>
#include <mbedtls/ssl_ciphersuites.h>
#include <errno.h>

#define SIRENA_LWM2M_SECURITY_OBJECT_ID 0U
#define SIRENA_LWM2M_IDENTITY_RESOURCE 3U
#define SIRENA_LWM2M_SECRET_KEY_RESOURCE 5U

void sirena_lwm2m_credentials_release(sec_tag_t tag)
{
    (void)tls_credential_delete(tag, TLS_CREDENTIAL_PSK);
    (void)tls_credential_delete(tag, TLS_CREDENTIAL_PSK_ID);
}

int sirena_lwm2m_credentials_register(sec_tag_t tag)
{
    const void *identity = NULL;
    const void *secret_key = NULL;
    size_t identity_length = 0U;
    size_t secret_key_length = 0U;
    int rc;

    rc = sirena_lwm2m_get_opaque_ref(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                      SIRENA_LWM2M_IDENTITY_RESOURCE, &identity,
                                      &identity_length);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_get_opaque_ref(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                      SIRENA_LWM2M_SECRET_KEY_RESOURCE, &secret_key,
                                      &secret_key_length);
    if (rc != 0) {
        return rc;
    }

    if (identity_length == 0U) {
        return -ENOENT;
    }

    if (identity_length > CONFIG_SIRENA_LWM2M_PSK_IDENTITY_MAX) {
        return -EINVAL;
    }

    if (secret_key_length == 0U) {
        return -ENOENT;
    }

    if (secret_key_length > CONFIG_SIRENA_LWM2M_PSK_KEY_MAX) {
        return -EINVAL;
    }

    if (secret_key_length > MBEDTLS_PSK_MAX_LEN) {
        return -ENOMEM;
    }

    sirena_lwm2m_credentials_release(tag);

    rc = tls_credential_add(tag, TLS_CREDENTIAL_PSK, secret_key, secret_key_length);
    if (rc != 0) {
        return rc;
    }

    rc = tls_credential_add(tag, TLS_CREDENTIAL_PSK_ID, identity, identity_length);
    if (rc != 0) {
        sirena_lwm2m_credentials_release(tag);

        return rc;
    }

    return 0;
}
