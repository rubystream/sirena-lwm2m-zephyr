#include "sirena_lwm2m_credentials.h"

#ifdef sirena_lwm2m_credentials_register
#undef sirena_lwm2m_credentials_register
#endif
#include <sirena/lwm2m/objects.h>
#include <zephyr/sys/util.h>
#include <mbedtls/ssl_ciphersuites.h>
#include <errno.h>

#define SIRENA_LWM2M_SECURITY_OBJECT_ID 0U
#define SIRENA_LWM2M_SECURITY_MODE_RESOURCE 2U
#define SIRENA_LWM2M_SECURITY_IDENTITY_RESOURCE 3U
#define SIRENA_LWM2M_SECURITY_SERVER_PUBLIC_KEY_RESOURCE 4U
#define SIRENA_LWM2M_SECURITY_SECRET_KEY_RESOURCE 5U
#define SIRENA_LWM2M_SECURITY_CLIENT_CERTIFICATE_RESOURCE SIRENA_LWM2M_SECURITY_IDENTITY_RESOURCE
#define SIRENA_LWM2M_SECURITY_TRUST_ANCHOR_RESOURCE SIRENA_LWM2M_SECURITY_SERVER_PUBLIC_KEY_RESOURCE
#define SIRENA_LWM2M_SECURITY_PRIVATE_KEY_RESOURCE SIRENA_LWM2M_SECURITY_SECRET_KEY_RESOURCE
#define SIRENA_LWM2M_SECURITY_PSK_IDENTITY_RESOURCE SIRENA_LWM2M_SECURITY_IDENTITY_RESOURCE
#define SIRENA_LWM2M_SECURITY_PSK_SECRET_RESOURCE SIRENA_LWM2M_SECURITY_SECRET_KEY_RESOURCE
#define SIRENA_LWM2M_CLIENT_CERTIFICATE_RESOURCE SIRENA_LWM2M_SECURITY_CLIENT_CERTIFICATE_RESOURCE
#define SIRENA_LWM2M_TRUST_ANCHOR_RESOURCE SIRENA_LWM2M_SECURITY_TRUST_ANCHOR_RESOURCE
#define SIRENA_LWM2M_PRIVATE_KEY_RESOURCE SIRENA_LWM2M_SECURITY_PRIVATE_KEY_RESOURCE
#define SIRENA_LWM2M_IDENTITY_RESOURCE SIRENA_LWM2M_SECURITY_IDENTITY_RESOURCE
#define SIRENA_LWM2M_SECRET_KEY_RESOURCE SIRENA_LWM2M_SECURITY_SECRET_KEY_RESOURCE

void sirena_lwm2m_credentials_release(sec_tag_t tag)
{
    (void)tls_credential_delete(tag, TLS_CREDENTIAL_PSK);
    (void)tls_credential_delete(tag, TLS_CREDENTIAL_PSK_ID);
    (void)tls_credential_delete(tag, TLS_CREDENTIAL_PUBLIC_CERTIFICATE);
    (void)tls_credential_delete(tag, TLS_CREDENTIAL_PRIVATE_KEY);
    (void)tls_credential_delete(tag, TLS_CREDENTIAL_CA_CERTIFICATE);
}

static int psk_credentials_register(sec_tag_t tag)
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
        sirena_lwm2m_credentials_release(tag);

        return rc;
    }

    rc = tls_credential_add(tag, TLS_CREDENTIAL_PSK_ID, identity, identity_length);
    if (rc != 0) {
        sirena_lwm2m_credentials_release(tag);

        return rc;
    }

    return 0;
}

static int certificate_credentials_register(sec_tag_t tag)
{
    const void *client_certificate = NULL;
    const void *trust_anchor = NULL;
    const void *private_key = NULL;
    size_t client_certificate_length = 0U;
    size_t trust_anchor_length = 0U;
    size_t private_key_length = 0U;
    int rc;

    rc = sirena_lwm2m_get_opaque_ref(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                      SIRENA_LWM2M_CLIENT_CERTIFICATE_RESOURCE,
                                      &client_certificate, &client_certificate_length);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_get_opaque_ref(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                      SIRENA_LWM2M_TRUST_ANCHOR_RESOURCE,
                                      &trust_anchor, &trust_anchor_length);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_get_opaque_ref(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                      SIRENA_LWM2M_PRIVATE_KEY_RESOURCE,
                                      &private_key, &private_key_length);
    if (rc != 0) {
        return rc;
    }

    if (client_certificate_length == 0U) {
        return -ENOENT;
    }

    if (trust_anchor_length == 0U) {
        return -ENOENT;
    }

    if (private_key_length == 0U) {
        return -ENOENT;
    }

    if (client_certificate_length > CONFIG_SIRENA_LWM2M_CERTIFICATE_MAX) {
        return -EINVAL;
    }

    if (trust_anchor_length > CONFIG_SIRENA_LWM2M_CERTIFICATE_MAX) {
        return -EINVAL;
    }

    if (private_key_length > CONFIG_SIRENA_LWM2M_CERTIFICATE_MAX) {
        return -EINVAL;
    }

    sirena_lwm2m_credentials_release(tag);

    rc = tls_credential_add(tag, TLS_CREDENTIAL_PUBLIC_CERTIFICATE,
                            client_certificate, client_certificate_length);
    if (rc != 0) {
        sirena_lwm2m_credentials_release(tag);

        return rc;
    }

    rc = tls_credential_add(tag, TLS_CREDENTIAL_CA_CERTIFICATE, trust_anchor,
                            trust_anchor_length);
    if (rc != 0) {
        sirena_lwm2m_credentials_release(tag);

        return rc;
    }

    rc = tls_credential_add(tag, TLS_CREDENTIAL_PRIVATE_KEY,
                            private_key, private_key_length);
    if (rc != 0) {
        sirena_lwm2m_credentials_release(tag);

        return rc;
    }

    return 0;
}

static int rpk_credentials_register(sec_tag_t tag)
{
    const void *client_public_key = NULL;
    const void *peer_public_key = NULL;
    size_t client_public_key_length = 0U;
    size_t peer_public_key_length = 0U;
    int rc;

    rc = sirena_lwm2m_get_opaque_ref(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                      SIRENA_LWM2M_IDENTITY_RESOURCE,
                                      &client_public_key,
                                      &client_public_key_length);
    if (rc != 0) {
        return rc;
    }

    rc = sirena_lwm2m_get_opaque_ref(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                      SIRENA_LWM2M_TRUST_ANCHOR_RESOURCE,
                                      &peer_public_key, &peer_public_key_length);
    if (rc != 0) {
        return rc;
    }

    if (client_public_key_length == 0U) {
        return -ENOENT;
    }

    if (peer_public_key_length == 0U) {
        return -ENOENT;
    }

    if (client_public_key_length > CONFIG_SIRENA_LWM2M_CERTIFICATE_MAX) {
        return -EINVAL;
    }

    if (peer_public_key_length > CONFIG_SIRENA_LWM2M_CERTIFICATE_MAX) {
        return -EINVAL;
    }

    sirena_lwm2m_credentials_release(tag);

    rc = tls_credential_add(tag, TLS_CREDENTIAL_PUBLIC_CERTIFICATE,
                            client_public_key, client_public_key_length);
    if (rc != 0) {
        sirena_lwm2m_credentials_release(tag);

        return rc;
    }

    rc = tls_credential_add(tag, TLS_CREDENTIAL_CA_CERTIFICATE,
                            peer_public_key, peer_public_key_length);
    if (rc != 0) {
        sirena_lwm2m_credentials_release(tag);

        return rc;
    }

    return 0;
}


int sirena_lwm2m_credentials_register_mode(sec_tag_t tag, int64_t mode)
{
    switch (mode) {
    case SIRENA_LWM2M_SECURITY_MODE_PSK:
        return psk_credentials_register(tag);

    case SIRENA_LWM2M_SECURITY_MODE_CERTIFICATE:
        return certificate_credentials_register(tag);

    case SIRENA_LWM2M_SECURITY_MODE_RPK:
        return rpk_credentials_register(tag);

    case SIRENA_LWM2M_SECURITY_MODE_NOSEC:
        sirena_lwm2m_credentials_release(tag);

        return 0;

    default:
        return -ENOTSUP;
    }
}


int sirena_lwm2m_credentials_register_one(sec_tag_t tag)
{
    int64_t mode;
    int rc;

    rc = sirena_lwm2m_get_integer(SIRENA_LWM2M_SECURITY_OBJECT_ID, 0U,
                                  SIRENA_LWM2M_SECURITY_MODE_RESOURCE, &mode);
    if (rc != 0) {
        return rc;
    }

    return sirena_lwm2m_credentials_register_mode(tag, mode);
}
