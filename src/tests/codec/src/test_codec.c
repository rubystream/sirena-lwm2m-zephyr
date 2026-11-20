#include <zephyr/ztest.h>
#include <sirena/lwm2m/codec.h>
#include <string.h>
#include <errno.h>

ZTEST(sirena_lwm2m_codec, test_supported_and_mime)
{
    uint16_t fmt = 0;
    zassert_true(sirena_lwm2m_content_format_supported(SIRENA_LWM2M_CF_TLV),
                 "tlv supported");
    zassert_equal(sirena_lwm2m_content_format_from_mime(
                      "application/vnd.oma.lwm2m+tlv", &fmt), 0,
                  "mime parse failed");
    zassert_equal(fmt, SIRENA_LWM2M_CF_TLV, "mime mismatch");
    zassert_str_equal(sirena_lwm2m_content_format_to_mime(
                          SIRENA_LWM2M_CF_PLAIN_TEXT), "text/plain");
}

ZTEST(sirena_lwm2m_codec, test_plain_roundtrip)
{
    struct sirena_lwm2m_codec_value v;
    char out[64];
    size_t len = sizeof(out);
    char in[] = "hello";
    int rc;

    memset(&v, 0, sizeof(v));
    v.id = 0;
    v.type = SIRENA_LWM2M_VALUE_STRING;
    v.data.string.value = in;
    v.data.string.length = strlen(in);
    rc = sirena_lwm2m_codec_encode(SIRENA_LWM2M_CF_PLAIN_TEXT, &v, 1, out,
                                   &len);
    zassert_equal(rc, 0, "encode failed");
    zassert_equal(len, 5U, "len wrong");
    zassert_mem_equal(out, "hello", 5, "content wrong");

    struct sirena_lwm2m_codec_value dec[1];
    size_t count = 1;
    rc = sirena_lwm2m_codec_decode(SIRENA_LWM2M_CF_PLAIN_TEXT, out, len, dec,
                                   &count);
    zassert_equal(rc, 0, "decode failed");
    zassert_equal(count, 1U, "count wrong");
    zassert_equal(dec[0].data.string.length, 5U, "dec len wrong");
}

ZTEST(sirena_lwm2m_codec, test_opaque_roundtrip)
{
    uint8_t data[] = {1,2,3};
    uint8_t out[64];
    size_t len = sizeof(out);
    struct sirena_lwm2m_codec_value v;
    int rc;
    memset(&v,0,sizeof(v));
    v.id=1;
    v.type=SIRENA_LWM2M_VALUE_OPAQUE;
    v.data.opaque.value=data;
    v.data.opaque.length=3;
    rc=sirena_lwm2m_codec_encode(SIRENA_LWM2M_CF_OPAQUE,&v,1,out,&len);
    zassert_equal(rc,0,"enc");
    zassert_equal(len,3U,"len");

    struct sirena_lwm2m_codec_value dec[1];
    size_t count=1;
    rc=sirena_lwm2m_codec_decode(SIRENA_LWM2M_CF_OPAQUE,out,len,dec,&count);
    zassert_equal(rc,0,"dec");
    zassert_equal(dec[0].data.opaque.length,3U,"deco len");
}

ZTEST(sirena_lwm2m_codec, test_tlv_roundtrip)
{
    struct sirena_lwm2m_codec_value v;
    uint8_t out[64];
    size_t len=sizeof(out);
    int rc;
    memset(&v,0,sizeof(v));
    v.id=5;
    v.type=SIRENA_LWM2M_VALUE_INTEGER;
    v.data.integer=42;
    rc=sirena_lwm2m_codec_encode(SIRENA_LWM2M_CF_TLV,&v,1,out,&len);
    zassert_equal(rc,0,"enc");
    zassert_true(len>0,"empty");

    struct sirena_lwm2m_codec_value dec[1];
    size_t count=1;
    rc=sirena_lwm2m_codec_decode(SIRENA_LWM2M_CF_TLV,out,len,dec,&count);
    zassert_equal(rc,0,"dec");
    zassert_equal(count,1U,"count");
    zassert_equal(dec[0].id,5U,"id");
    zassert_equal(dec[0].data.integer,42,"val");
}

ZTEST(sirena_lwm2m_codec, test_cbor_roundtrip_dispatch)
{
    struct sirena_lwm2m_codec_value v;
    uint8_t out[64];
    size_t len=sizeof(out);
    int rc;
    memset(&v,0,sizeof(v));
    v.id=2;
    v.type=SIRENA_LWM2M_VALUE_OPAQUE;
    const uint8_t d[]={0xAB,0xCD};
    v.data.opaque.value=d;
    v.data.opaque.length=2;

    rc=sirena_lwm2m_codec_encode(SIRENA_LWM2M_CF_CBOR,&v,1,out,&len);
    zassert_equal(rc,0,"enc");

    struct sirena_lwm2m_codec_value dec[1];
    size_t count=1;
    rc=sirena_lwm2m_codec_decode(SIRENA_LWM2M_CF_CBOR,out,len,dec,&count);
    zassert_equal(rc, -ENOTSUP, "dec");
}

ZTEST(sirena_lwm2m_codec, test_json_contains_value)
{
    struct sirena_lwm2m_codec_value v;
    char out[64];
    size_t len=sizeof(out);
    int rc;
    memset(&v,0,sizeof(v));
    v.id=5;
    v.type=SIRENA_LWM2M_VALUE_INTEGER;
    v.data.integer=42;
    rc=sirena_lwm2m_codec_encode(SIRENA_LWM2M_CF_SENML_JSON,&v,1,out,&len);
    zassert_equal(rc,0,"enc");
    zassert_true(strstr(out,"42")!=NULL,"missing value");
}

ZTEST(sirena_lwm2m_codec, test_malformed_and_dispatch)
{
    char out[64];
    size_t len=sizeof(out);
    struct sirena_lwm2m_codec_value v;
    memset(&v,0,sizeof(v));
    v.type=SIRENA_LWM2M_VALUE_STRING;
    v.data.string.value="x";
    v.data.string.length=1;
    int rc=sirena_lwm2m_codec_encode(999,&v,1,out,&len);
    zassert_equal(rc,-ENOTSUP,"bad format accepted");

    uint16_t accepted[]={SIRENA_LWM2M_CF_OPAQUE,SIRENA_LWM2M_CF_TLV};
    int chosen=sirena_lwm2m_content_format_negotiate(accepted,2,
                                                         SIRENA_LWM2M_CF_TLV,
                                                         SIRENA_LWM2M_VALUE_INTEGER);
    zassert_equal(chosen,SIRENA_LWM2M_CF_TLV,"negotiate failed");
}

ZTEST(sirena_lwm2m_codec, test_tlv_truncated_decode)
{
    struct sirena_lwm2m_codec_value v;
    uint8_t out[64];
    size_t len = sizeof(out);
    int rc;
    memset(&v, 0, sizeof(v));
    v.id = 5;
    v.type = SIRENA_LWM2M_VALUE_INTEGER;
    v.data.integer = 42;
    rc = sirena_lwm2m_codec_encode(SIRENA_LWM2M_CF_TLV, &v, 1, out, &len);
    zassert_equal(rc, 0, "enc");

    struct sirena_lwm2m_codec_value dec[1];
    size_t count = 1;
    rc = sirena_lwm2m_codec_decode(SIRENA_LWM2M_CF_TLV, out, len - 1, dec,
                                   &count);
    zassert_not_equal(rc, 0, "truncated accepted");
}

ZTEST(sirena_lwm2m_codec, test_capacity_overflow)
{
    struct sirena_lwm2m_codec_value v;
    uint8_t out[1];
    size_t len = sizeof(out);
    int rc;
    memset(&v, 0, sizeof(v));
    v.id = 5;
    v.type = SIRENA_LWM2M_VALUE_INTEGER;
    v.data.integer = 42;
    rc = sirena_lwm2m_codec_encode(SIRENA_LWM2M_CF_TLV, &v, 1, out, &len);
    zassert_equal(rc, -ENOMEM, "overflow not rejected");
}

ZTEST(sirena_lwm2m_codec, test_negotiate_failure)
{
    uint16_t accepted[] = {999};
    int chosen = sirena_lwm2m_content_format_negotiate(accepted, 1, SIRENA_LWM2M_CF_TLV,
                                                        SIRENA_LWM2M_VALUE_INTEGER);
    zassert_equal(chosen, -ENOTSUP, "negotiate accepted unsupported");
    chosen = sirena_lwm2m_content_format_negotiate(NULL, 0, SIRENA_LWM2M_CF_PLAIN_TEXT,
                                                    SIRENA_LWM2M_VALUE_INTEGER);
    zassert_equal(chosen, SIRENA_LWM2M_CF_TLV, "negotiate did not fall back");
    chosen = sirena_lwm2m_content_format_negotiate(NULL, 0, SIRENA_LWM2M_CF_PLAIN_TEXT,
                                                    SIRENA_LWM2M_VALUE_NONE);
    zassert_equal(chosen, -ENOTSUP, "negotiate none");
}

ZTEST(sirena_lwm2m_codec, test_negotiate_refuses_a_format_the_codec_cannot_encode)
{
    uint16_t accepted[] = {SIRENA_LWM2M_CF_PLAIN_TEXT};
    int chosen = sirena_lwm2m_content_format_negotiate(accepted, 1, SIRENA_LWM2M_CF_TLV,
                                                        SIRENA_LWM2M_VALUE_INTEGER);
    zassert_equal(chosen, -ENOTSUP, "the plain text format cannot encode an integer");
    chosen = sirena_lwm2m_content_format_negotiate(accepted, 1, SIRENA_LWM2M_CF_TLV,
                                                    SIRENA_LWM2M_VALUE_STRING);
    zassert_equal(chosen, SIRENA_LWM2M_CF_PLAIN_TEXT, "the plain text format encodes a string");
    zassert_true(sirena_lwm2m_codec_can_encode(SIRENA_LWM2M_CF_TLV,
                                                 SIRENA_LWM2M_VALUE_INTEGER),
                 "the tlv format encodes an integer");
    zassert_false(sirena_lwm2m_codec_can_encode(SIRENA_LWM2M_CF_OPAQUE,
                                                  SIRENA_LWM2M_VALUE_STRING),
                  "the opaque format does not encode a string");
}

ZTEST(sirena_lwm2m_codec, test_tlv_16bit_id)
{
    struct sirena_lwm2m_codec_value v;
    uint8_t out[64];
    size_t len = sizeof(out);
    int rc;
    memset(&v, 0, sizeof(v));
    v.id = 300;
    v.type = SIRENA_LWM2M_VALUE_INTEGER;
    v.data.integer = 7;
    rc = sirena_lwm2m_codec_encode(SIRENA_LWM2M_CF_TLV, &v, 1, out, &len);
    zassert_equal(rc, 0, "enc");

    struct sirena_lwm2m_codec_value dec[1];
    size_t count = 1;
    rc = sirena_lwm2m_codec_decode(SIRENA_LWM2M_CF_TLV, out, len, dec,
                                   &count);
    zassert_equal(rc, 0, "dec");
    zassert_equal(count, 1U, "count");
    zassert_equal(dec[0].id, 300U, "id");
    zassert_equal(dec[0].data.integer, 7, "val");
}

ZTEST(sirena_lwm2m_codec, test_encode_zero_count_rejected)
{
    struct sirena_lwm2m_codec_value v;
    char out[8];
    size_t len = sizeof(out);
    memset(&v, 0, sizeof(v));
    int rc = sirena_lwm2m_codec_encode(SIRENA_LWM2M_CF_TLV, &v, 0, out, &len);
    zassert_equal(rc, -EINVAL, "zero count accepted");
}

ZTEST(sirena_lwm2m_codec, test_string_float_encode)
{
    struct sirena_lwm2m_codec_value v[2];
    char out[64];
    size_t len = sizeof(out);
    int rc;
    memset(v, 0, sizeof(v));
    v[0].id = 1;
    v[0].type = SIRENA_LWM2M_VALUE_STRING;
    v[0].data.string.value = "abc";
    v[0].data.string.length = 3;
    v[1].id = 2;
    v[1].type = SIRENA_LWM2M_VALUE_FLOAT;
    v[1].data.floating_point = 1.5;
    rc = sirena_lwm2m_codec_encode(SIRENA_LWM2M_CF_JSON, v, 2, out, &len);
    zassert_equal(rc, 0, "enc");
    zassert_true(strstr(out, "abc") != NULL, "string missing");
    zassert_true(strstr(out, "1.5") != NULL, "float missing");
}

ZTEST_SUITE(sirena_lwm2m_codec, NULL, NULL, NULL, NULL, NULL);
