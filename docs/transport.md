# Transport Bindings

The transport layer connects the LwM2M client core to a server. It is split in
five parts:

| Part | Header | Implementation |
| --- | --- | --- |
| Transport interface | `src/include/sirena/lwm2m/transport.h` | `src/lib/sirena_lwm2m_transport.c` |
| Engine thread | `src/include/sirena/lwm2m/engine.h` | `src/lib/sirena_lwm2m_engine.c` |
| CoAP/UDP binding | `src/include/sirena/lwm2m/coap.h` | `src/lib/sirena_lwm2m_coap.c` |
| CoAP/DTLS binding | `src/include/sirena/lwm2m/dtls.h` | `src/lib/sirena_lwm2m_dtls.c` |
| Shared exchange | `src/lib/sirena_lwm2m_exchange.h` | `src/lib/sirena_lwm2m_exchange.c` |
| CoAP endpoint | `src/lib/sirena_lwm2m_coap_endpoint.h` | `src/lib/sirena_lwm2m_coap_endpoint.c` |

The transport interface is what the reusable core talks to. It carries the
registration lifecycle only and never exposes a CoAP type to the core, so the
binding can be replaced without touching the core.

The transport layer also selects the binding. The application configures the
LwM2M server URI once, in the Security object, and the transport layer picks
the plaintext or the secure binding from its scheme.

The shared exchange holds the parts of the registration that both bindings use:
the registration payload, the request paths, the server address and the request
itself with the mapping of its response. A change to the response mapping or to
the path handling is therefore made once.

Both bindings share one CoAP endpoint, which owns the socket of the transport.
Zephyr's CoAP client cannot be used for that: it is response only. It
dispatches an incoming datagram by the token of a request the client itself
sent and answers a request with no matching token with a reset, so a server
initiated request never reaches the application. It also owns the thread that
polls the socket. The endpoint therefore owns the receive path of the socket
itself, sends the requests of the registration lifecycle and dispatches the
requests the server sends into the protocol core. The server reaches the device
management interface on the same socket the client registered on, as OMA LwM2M
1.2 requires. The protocol core is documented in
[Protocol Core](protocol.md).

## Transport interface

```c
struct sirena_lwm2m_transport_ops {
    int (*register_client)(void);
    int (*update)(void);
    int (*deregister)(void);
    int (*poll)(int timeout_ms);
    int (*notify)(uint16_t object_id, uint16_t instance_id, uint16_t resource_id);
};

int sirena_lwm2m_transport_bind(const struct sirena_lwm2m_transport_ops *ops);
int sirena_lwm2m_transport_register(void);
int sirena_lwm2m_transport_update(void);
int sirena_lwm2m_transport_deregister(void);

int sirena_lwm2m_transport_set_endpoint_name(const char *name);
const char *sirena_lwm2m_transport_endpoint_name(void);
int sirena_lwm2m_transport_start(void);
```

The `poll` entry point lets the engine thread watch the socket of the binding.
The engine calls it while it waits for work, so a request the server sends is
dispatched even when no lifecycle call is running. The `notify` entry point
publishes a resource value to the observations of the binding and returns the
number of the observations it notified.

`sirena_lwm2m_transport_bind()` attaches a binding to the transport interface
and starts the engine thread. The attachment happens in engine context, so
the binding of a running transport is never replaced behind the back of the
engine thread. It refuses with `-EBUSY` to replace the binding of a running
transport with another one. The three lifecycle calls queue the request on the
engine thread and wait for its completion, so they return the result of the
transport work to the caller.

A lifecycle call without a binding, or a call made while the engine thread
is not running, returns `-ENOTSUP`. A build without a transport binding has no
binding to attach, so its lifecycle calls fail the same way.

`sirena_lwm2m_transport_set_endpoint_name()` sets the LwM2M Endpoint Client
Name. The name is not carried by any OMA object, so the application must
supply it. A `NULL` name is rejected with `-EINVAL`, an empty name clears the
endpoint name, and a name longer than `CONFIG_SIRENA_LWM2M_ENDPOINT_NAME_MAX`
is rejected with `-EINVAL`. The name belongs to the transport layer, because
the binding that is selected later needs it.
`sirena_lwm2m_transport_endpoint_name()` returns it.

`sirena_lwm2m_transport_start()` selects the binding and starts it. It reads
the LwM2M server URI and the security mode from the Security object in engine
context and starts the binding that matches them. It is the entry point for an
application that lets the transport pick the binding; an application that
wants the plaintext binding explicitly can call `sirena_lwm2m_coap_start()`
instead. The call queues the work on the engine thread and waits for its
completion.

### Binding selection

| Server URI scheme | Security mode | Binding | Result |
| --- | --- | --- | --- |
| `coaps://` | 0, Pre-Shared Key | CoAP/DTLS | The DTLS binding starts. |
| `coap://` | 3, NoSec | CoAP/UDP | The plaintext binding starts, but only when the noSec development gate is open. |
| `coap://` | 0, Pre-Shared Key | none | `-EINVAL`, a pre-shared key server needs the secure scheme. |
| `coaps://` | 3, NoSec | none | `-EINVAL`, a noSec server has no secure transport. |
| any | 1, 2, 4 | none | `-ENOTSUP`, the security mode is not implemented. |

The selection never falls back: a server that needs DTLS is never contacted in
the clear, and a noSec server is never contacted over DTLS. The binding that
is selected enforces the security mode on its own, so a direct call of
`sirena_lwm2m_coap_start()` cannot reach a server in the clear either.
Starting the transport again with another server URI stops the binding that
runs and starts the other one.

## Engine thread

The engine owns the transport. It runs one dedicated thread with a bounded
static request queue:

```c
int sirena_lwm2m_engine_start(sirena_lwm2m_engine_handler_t handler);
int sirena_lwm2m_engine_submit(enum sirena_lwm2m_engine_operation operation, void *payload);
int sirena_lwm2m_engine_set_poll_fn(sirena_lwm2m_engine_poll_fn poll_fn);
k_tid_t sirena_lwm2m_engine_thread_id(void);
```

The operations of the engine are the transport operations:

| Operation | Work |
| --- | --- |
| `SIRENA_LWM2M_ENGINE_OPERATION_START` | Select the binding from the Security object and start it. |
| `SIRENA_LWM2M_ENGINE_OPERATION_START_COAP` | Start the plaintext binding. |
| `SIRENA_LWM2M_ENGINE_OPERATION_START_DTLS` | Start the secure binding. |
| `SIRENA_LWM2M_ENGINE_OPERATION_STOP_COAP` | Stop the plaintext binding. |
| `SIRENA_LWM2M_ENGINE_OPERATION_STOP_DTLS` | Stop the secure binding. |
| `SIRENA_LWM2M_ENGINE_OPERATION_REGISTER` | Register with the server. |
| `SIRENA_LWM2M_ENGINE_OPERATION_UPDATE` | Update the registration. |
| `SIRENA_LWM2M_ENGINE_OPERATION_DEREGISTER` | Deregister from the server. |
| `SIRENA_LWM2M_ENGINE_OPERATION_BIND` | Attach a binding to the transport interface. |
| `SIRENA_LWM2M_ENGINE_OPERATION_NOTIFY` | Publish a resource value to the observations of the binding. |

`sirena_lwm2m_engine_thread_id()` returns the thread identifier of the engine
thread, or `NULL` when the engine thread does not run. It lets a caller prove
that a piece of work ran in engine context.

`sirena_lwm2m_engine_start()` registers the transport handler and starts the
engine thread. It returns `-EINVAL` for a `NULL` handler and `-EALREADY`
when the engine thread is already running. The transport interface calls it
from `sirena_lwm2m_transport_bind()`; an application does not call it.

- The queue holds `CONFIG_SIRENA_LWM2M_ENGINE_QUEUE_SIZE` requests. There is no
  heap use: the queue and its entries are static.
- `sirena_lwm2m_engine_submit()` reserves a free queue entry, fills it, queues
  it and waits on the entry semaphore for the engine to complete it. Requests
  are processed in the order they were queued.
- When every queue entry is in use the submit returns `-EAGAIN`.
- A submit without a running engine thread returns `-ENOTSUP`.
- The engine thread runs the transport work, so the binding is never called
  re-entrantly from an application thread.

The stack size and the priority of the engine thread are configurable with
`CONFIG_SIRENA_LWM2M_ENGINE_STACK_SIZE` and `CONFIG_SIRENA_LWM2M_ENGINE_PRIORITY`.

The engine thread watches the socket of the transport while it waits for work.
The poll function of the binding is called with the interval of
`CONFIG_SIRENA_LWM2M_ENGINE_POLL_INTERVAL_MS`, so a request the server sends is
dispatched even when no lifecycle call is running. The interval bounds the
latency of a server request and the resolution of the retransmission timers of a
pending request.

### Threading contract

The application calls the transport API; the engine thread performs the work.
The binding is never called re-entrantly from an application thread. The
start and the stop of a binding are engine operations, so the application
thread only queues them and waits for their completion.

The object registry and the built-in objects API are not thread-safe. The
binding reads the Security and Server objects from the engine thread, so the
application must finish provisioning and configuring the client before it
starts the transport. The DTLS binding reads the pre-shared key credentials
from the Security object and runs the DTLS handshake in the engine thread as
well.

### Callback context

The transport handler of the engine thread, the binding entry points and every
transport decision run in engine context.

The engine thread owns the socket of the transport. It polls the socket, it
sends the requests of the registration lifecycle and it dispatches the requests
the server sends. Every protocol operation therefore runs in engine context,
including the answer to a server request and the sending of a notification.

No application callback is invoked in this slice, apart from the execute
handler of the application, which the protocol core calls in engine context.
The transport API returns the result of the transport work to the caller.

## CoAP/UDP binding

The binding implements the LwM2M 1.2 registration interface over plain CoAP
over UDP.

```c
int sirena_lwm2m_coap_set_endpoint_name(const char *name);
int sirena_lwm2m_coap_start(void);
int sirena_lwm2m_coap_stop(void);
int sirena_lwm2m_coap_get_location_path(char *buffer, size_t *length);
```

- `sirena_lwm2m_coap_set_endpoint_name()` sets the LwM2M Endpoint Client Name
  in the transport layer, so both bindings use the same name. A `NULL` name is
  rejected with `-EINVAL`, an empty name clears the endpoint name, and a name
  longer than `CONFIG_SIRENA_LWM2M_ENDPOINT_NAME_MAX` is rejected with
  `-EINVAL`. Registering without an endpoint name fails with `-EINVAL` and no
  request is sent.
- `sirena_lwm2m_coap_start()` reads the server URI and the security mode from
  the Security object, creates the UDP socket, initializes the CoAP client and
  binds the binding to the transport interface, which starts the engine
  thread. The work runs in the engine thread, so the call returns the result
  of the work to the application. It is idempotent and returns `0` when the
  transport is already running. A server URI that does not use the `coap://`
  scheme is rejected with `-EINVAL`, and a `coaps://` URI with `-ENOTSUP`. A
  Security object that does not exist yet is reported with `-ENOENT`, so the
  application provisions the client before it starts the transport.
- `sirena_lwm2m_coap_stop()` closes the UDP socket and drops the registration
  location path. The socket belongs to the engine thread, so the call queues
  the stop on the engine thread and waits for its completion. The engine
  thread keeps running.
- `sirena_lwm2m_coap_get_location_path()` returns the location path the server
  assigned to the registration, for example `/rd/1`. It returns the character
  count without the terminating NUL, like the other string getters of the
  project.

### The security mode

The binding serves a server that uses the NoSec security mode and nothing
else. It reads the security mode from the Security object resource 2 when it
starts and before every request, so a server that needs a secure transport is
never contacted in the clear:

| Security mode | Result |
| --- | --- |
| 3, NoSec | The server is served, but only when the noSec development gate is open. |
| 0, Pre-Shared Key | `-EINVAL`, the server needs the secure scheme and the DTLS binding. |
| 1, 2, 4 | `-ENOTSUP`, the security mode is not implemented. |

The check belongs to the binding and not only to the selection of the
transport, so calling `sirena_lwm2m_coap_start()` directly is safe as well.

### Where the registration parameters come from

| Parameter | Source |
| --- | --- |
| Server address | Security object resource 0, the LwM2M Server URI, for example `coap://192.0.2.10:5683` |
| Lifetime | Server object instance 0, resource 1 |
| Binding mode | `U`, hard-coded for this binding |
| LwM2M version | `1.2`, hard-coded for this binding |
| Endpoint name | The application, through `sirena_lwm2m_transport_set_endpoint_name()` |
| Objects and instances | The object registry, at the time of the registration |

The server URI is read from the Security object for every request. Only the
`coap://` scheme is supported. A `coaps://` URI is rejected with `-ENOTSUP`
and no request is sent: the binding never falls back to plaintext. The host
must be a numeric IPv4 address; the port defaults to 5683 when the URI carries
no port. Host names and IPv6 addresses are not supported by this binding.

### Registration lifecycle

| Operation | Method | URI |
| --- | --- | --- |
| Register | POST | `/rd?ep={endpoint}&lt={lifetime}&lwm2m=1.2&b=U` |
| Update | POST | `/{location}?lt={lifetime}&b=U` |
| Deregister | DELETE | `/{location}` |

The Register payload is `application/link-format` and lists the objects and
object instances the client holds, for example `</1/0>,</3/0>`. The list is
built from the object registry at the time of the registration, so it follows
what the application registered. The Security object (id 0) is never part of
the list, as required by LwM2M 1.2 Core 6.2.1. The Server (id 1) and the Device
(id 3) objects are part of it: the registration is refused with `-EINVAL`
when one of them has no instance in the registry, because the mandatory
objects have to be part of the payload.

The Update and the Deregister requests carry the Content-Format of the link
format, as the Register request does, also when they carry no payload.

The Update request carries the lifetime and the binding mode. It never carries
the endpoint name.

The server answers a successful registration with a `2.01 Created` response
that carries a Location-Path option. The binding stores the location path and
uses it for the Update and the Deregister requests. The LwM2M 1.0 style
Location option, CoAP option 7, is accepted as well when the Location-Path
option is absent. The location path is stored only for a successful response,
so a refused registration leaves no registration behind. A successful response
without a location path is rejected with `-EPROTO`, because the Update and
the Deregister requests could not be addressed.

The Deregister request clears the stored location path. A Deregister or an
Update without a prior registration fails with `-EINVAL`.

### Error codes

| Code | Meaning |
| --- | --- |
| `0` | The server accepted the request. |
| `-EINVAL` | The endpoint name is missing or invalid, the server URI is malformed, the server is provisioned with the Pre-Shared Key security mode and needs the secure scheme, no registration exists for Update or Deregister, the Server (1) or the Device (3) object instance is missing from the registry, or the request path does not fit the CoAP path buffer. |
| `-EALREADY` | The engine thread is already running. Only `sirena_lwm2m_engine_start()` returns it. |
| `-EAGAIN` | The engine request queue is full. |
| `-ENOTSUP` | The server URI uses the `coaps://` scheme, the host is not a numeric IPv4 address, no binding is bound to the transport interface, the engine thread is not running, or the security mode of the server is not implemented: Raw Public Key (1), Certificate (2) or Certificate with EST (4). |
| `-EPERM` | The noSec development gate is closed and the server is provisioned with the NoSec security mode. |
| `-ENOENT` | The Security object, the Server object or the resource that holds the server URI or the lifetime does not exist, or the server answered `4.04 Not Found`, which means that the registration is unknown to the server. |
| `-ENOMEM` | The registration payload or the request path does not fit the configured buffer. |
| `-EBADMSG` | The server answered `4.00 Bad Request`. |
| `-EACCES` | The server answered `4.03 Forbidden`. |
| `-EPROTO` | The server answered `4.12 Precondition Failed`, which means that it does not support the LwM2M version of the client, or the server accepted the registration without returning a location path. |
| `-EHOSTUNREACH` | The server answered `5.02 Bad Gateway`. |
| `-EIO` | The server answered any other status code. |
| `-ETIMEDOUT` | The server did not answer within `CONFIG_SIRENA_LWM2M_COAP_RESPONSE_TIMEOUT_MS`. |

The LwM2M specification labels the `4.03` response of the registration
interface "Bad Gateway", while CoAP defines `4.03` as Forbidden and `5.02` as
Bad Gateway. The binding maps both responses to distinct codes so that the
caller can tell them apart.

## CoAP/DTLS binding

The binding implements the LwM2M 1.2 registration interface over CoAP over
DTLS 1.2.

```c
int sirena_lwm2m_dtls_start(void);
int sirena_lwm2m_dtls_stop(void);
int sirena_lwm2m_dtls_get_location_path(char *buffer, size_t *length);
```

- `sirena_lwm2m_dtls_start()` checks the server URI and the security mode,
  binds the binding to the transport interface, which starts the engine
  thread. The work runs in the engine thread, so the call returns the result
  of the work to the application. It is idempotent and returns `0` when the
  transport is already running. A server URI that does not use the `coaps://`
  scheme is rejected with `-EINVAL`. A server that uses the NoSec security
  mode is rejected with `-EINVAL`, because a NoSec server has no secure
  transport, and a security mode that is not implemented with `-ENOTSUP`. A
  Security object that does not exist yet is reported with `-ENOENT`, so the
  application provisions the client before it starts the transport.
- `sirena_lwm2m_dtls_stop()` closes the DTLS session, releases the
  credentials and drops the registration location path. The session belongs to
  the engine thread, so the call queues the stop on the engine thread and
  waits for its completion. The engine thread keeps running.
- `sirena_lwm2m_dtls_get_location_path()` returns the location path the server
  assigned to the registration, for example `/rd/1`.

The registration lifecycle, the request paths, the query parameters and the
registration payload are the same as in the plaintext binding. The server
address comes from the Security object resource 0, the LwM2M Server URI, for
example `coaps://192.0.2.10:5684`. The port is 5684, the CoAP over UDP port
for a secure transport, when the URI carries no port. The host must be a
numeric IPv4 address.

### The credentials

The credentials come from the Security object of the server instance and from
nowhere else. The binding never takes them from application parameters that
bypass the Security object.

| Resource | Content |
| --- | --- |
| Security 0, resource 2 | The security mode, 0 for Pre-Shared Key |
| Security 0, resource 3 | The PSK identity |
| Security 0, resource 5 | The PSK |

The rules of the OMA LwM2M 1.2 Transport Bindings, section 5.2.9.1, apply:

- The PSK identity is stored in the "Public Key or Identity" resource. It is
  an opaque value of up to 128 bytes, `CONFIG_SIRENA_LWM2M_PSK_IDENTITY_MAX`.
- The PSK is stored in the "Secret Key" resource in raw binary form. A 16 byte
  (128 bit) key is recommended and keys of up to 64 bytes are supported,
  `CONFIG_SIRENA_LWM2M_PSK_KEY_MAX`.
- The "Server Public Key" resource is not used in the Pre-Shared Key mode.
- The client offers the `TLS_PSK_WITH_AES_128_CCM_8` ciphersuite and not the
  `TLS_PSK_WITH_AES_128_CBC_SHA256` ciphersuite.

mbed TLS limits the size of a pre-shared key with
`CONFIG_MBEDTLS_PSK_MAX_LEN`, which defaults to 32 bytes. An application that
provisions a longer key raises it to the same size.

The key material stays in the static storage of the Security object. The
binding registers the identity and the key with the TLS credential subsystem
under one secure tag, `CONFIG_SIRENA_LWM2M_DTLS_SEC_TAG`, and points the
credentials at the storage of the Security object, so the material is never
copied into a second buffer and never logged. The secure tag identifies one
set of credentials, so an application that uses credentials of its own picks a
tag that is not used already.
The credentials are released when the session ends and when the transport
stops, so a re-provisioning with other credentials cannot leave the old key
registered.

### The DTLS session

The binding opens a DTLS session when it registers and keeps it for the
Update and the Deregister requests. A registration opens a new session, so the
credentials are read from the Security object again and a re-provisioning with
other credentials takes effect on the next registration.

The session uses a DTLS 1.2 datagram socket, `NET_IPPROTO_DTLS_1_2`, with the
credentials of the secure tag, the `TLS_PSK_WITH_AES_128_CCM_8` ciphersuite
and no peer verification, because a pre-shared key server has no certificate
to verify. The handshake runs in the engine thread, like the rest of the
transport work.

### The noSec development gate

The NoSec security mode means that the traffic is not protected at all. It is
reachable only when `CONFIG_SIRENA_LWM2M_TRANSPORT_NOSEC` is enabled, and the
switch is off by default, so a production build cannot silently talk to a
server in the clear. The gate is meant for development and testing only.

The gate is enforced in the plaintext binding, which reads the security mode
of the server when it starts and before every request. With the gate closed, a
server that is provisioned with the NoSec security mode is refused with
`-EPERM` and no request is sent. The transport layer refuses the same server
with `-EPERM` when it selects the binding. The gate has no effect on the DTLS
binding: NoSec is not a DTLS security mode.

### Error codes

| Code | Meaning |
| --- | --- |
| `0` | The server accepted the request. |
| `-EINVAL` | The endpoint name is missing or invalid, the server URI is malformed or does not use the `coaps://` scheme, the server is provisioned with the NoSec security mode, the PSK identity or the PSK is longer than the configured limit, no registration exists for Update or Deregister, the Server (1) or the Device (3) object instance is missing from the registry, or the request path does not fit the CoAP path buffer. |
| `-EPERM` | The noSec development gate is closed and the server is provisioned with the NoSec security mode. The transport refuses to select the secure binding for such a server. |
| `-ENOTSUP` | The security mode of the server is not implemented: Raw Public Key (1), Certificate (2) or Certificate with EST (4). |
| `-ENOENT` | The Security object, the Server object or the resource that holds the server URI or the lifetime does not exist, the Security object holds no PSK identity or no PSK, or the server answered `4.04 Not Found`. |
| `-EACCES` | The DTLS handshake failed. The server rejected the pre-shared key or the key exchange did not complete. |
| `-ENOMEM` | The registration payload or the request path does not fit the configured buffer, or the PSK is longer than `CONFIG_MBEDTLS_PSK_MAX_LEN`. |
| `-EPROTO` | The server answered `4.12 Precondition Failed`, which means that it does not support the LwM2M version of the client, or the server accepted the registration without returning a location path. |
| `-ETIMEDOUT` | The server did not answer within `CONFIG_SIRENA_LWM2M_COAP_RESPONSE_TIMEOUT_MS`. |

A failed handshake sends no request: the server never sees the registration.

## Configuration

| Kconfig symbol | Purpose | Default |
| --- | --- | --- |
| `CONFIG_SIRENA_LWM2M_TRANSPORT_COAP` | Enables the CoAP/UDP binding. It depends on `CONFIG_COAP` and `CONFIG_COAP_CLIENT` and selects `CONFIG_COAP_EXTENDED_OPTIONS_LEN`. | `y` |
| `CONFIG_SIRENA_LWM2M_TRANSPORT_DTLS` | Enables the CoAP/DTLS binding. It depends on `CONFIG_COAP`, `CONFIG_COAP_CLIENT`, `CONFIG_NET_SOCKETS`, `CONFIG_MBEDTLS` and `CONFIG_NET_SOCKETS_SOCKOPT_TLS` and selects the mbed TLS PSK key exchange, the DTLS protocol and the CCM and SHA-256 algorithms. | `y` |
| `CONFIG_SIRENA_LWM2M_TRANSPORT_NOSEC` | Allows a server that uses the NoSec security mode. It is for development and testing only. | `n` |
| `CONFIG_SIRENA_LWM2M_PSK_IDENTITY_MAX` | Maximum size of the PSK identity. | 128 |
| `CONFIG_SIRENA_LWM2M_PSK_KEY_MAX` | Maximum size of the PSK key. | 64 |
| `CONFIG_SIRENA_LWM2M_DTLS_SEC_TAG` | Secure tag under which the DTLS binding registers the credentials. It has to be unique in the application. | 1 |
| `CONFIG_SIRENA_LWM2M_CREDENTIAL_MAX` | Size of the identity, public key and secret key buffers of the Security object. It is 128 when the DTLS binding is enabled. | 32 |
| `CONFIG_COAP_EXTENDED_OPTIONS_LEN_VALUE` | Length of a CoAP option value. The module sizes it so that the endpoint client name fits. | 64 |
| `CONFIG_SIRENA_LWM2M_ENGINE_QUEUE_SIZE` | Number of queued engine requests. | 8 |
| `CONFIG_SIRENA_LWM2M_ENGINE_STACK_SIZE` | Stack size of the engine thread. | 2048 |
| `CONFIG_SIRENA_LWM2M_ENGINE_PRIORITY` | Priority of the engine thread. | 5 |
| `CONFIG_SIRENA_LWM2M_ENDPOINT_NAME_MAX` | Maximum length of the endpoint client name. | 32 |
| `CONFIG_SIRENA_LWM2M_LOCATION_PATH_MAX` | Maximum length of the stored location path. | 64 |
| `CONFIG_SIRENA_LWM2M_REGISTRATION_PAYLOAD_MAX` | Maximum length of the registration payload. | 256 |
| `CONFIG_SIRENA_LWM2M_COAP_LOCATION_SEGMENTS` | Number of Location-Path segments the binding reads from a registration response. | 4 |
| `CONFIG_SIRENA_LWM2M_COAP_PATH_MAX` | Length of the CoAP request path the binding builds. | 128 |
| `CONFIG_SIRENA_LWM2M_COAP_RESPONSE_TIMEOUT_MS` | Time the binding waits for a server response. | 10000 |
| `CONFIG_SIRENA_LWM2M_ENGINE_POLL_INTERVAL_MS` | Interval the engine thread polls the transport socket with. | 20 |
| `CONFIG_SIRENA_LWM2M_COAP_MESSAGE_MAX` | Size of the buffer that holds a CoAP message. | 512 |

The DTLS binding needs mbed TLS with the PSK key exchange, so the application
enables `CONFIG_MBEDTLS` and `CONFIG_NET_SOCKETS_SOCKOPT_TLS`. The binding
selects the HMAC and the SHA-384 algorithms of mbed TLS. The TLS 1.2 master
secret is 48 bytes long and the static PSA key slots are sized by the largest
hash of the build, so the master secret only fits into a key slot when HMAC
and SHA-384 are enabled. An application that enables the mbed TLS heap
instead does not need them for the key slot size.

The registration request carries the endpoint client name in a URI-Query
option. Zephyr limits the length of an option value to
`CONFIG_COAP_EXTENDED_OPTIONS_LEN_VALUE` bytes, 13 by default, and drops
every longer option. The module sizes the value itself: with the CoAP
binding enabled it defaults to 64 bytes, which holds the maximum endpoint
client name of 32 characters and the `ep=` prefix. An application that
raises `CONFIG_SIRENA_LWM2M_ENDPOINT_NAME_MAX` has to raise
`CONFIG_COAP_EXTENDED_OPTIONS_LEN_VALUE` with it.

The request path holds the path and the query of the request. It has to fit
the path buffer of the Zephyr CoAP client, `CONFIG_COAP_CLIENT_MAX_PATH_LENGTH`,
which is 64 bytes by default. A request that does not fit is rejected with
`-EINVAL`.

## Not in this binding

- Content format negotiation on the registration payload.
- The periodic re-registration timer that drives the Update. The Update is
  implemented and reachable through the API, the scheduling policy belongs to
  the protocol layer.
- Host names and IPv6 addresses in the server URI.
- The Raw Public Key, the Certificate and the Certificate with EST security
  modes. They are refused with `-ENOTSUP`.
- The SNI resource of the Security object. It belongs to the certificate
  modes.

The observation, the notification and the read, write and execute operations
are documented in [Protocol Core](protocol.md).

## Tests

```sh
npm run test:coap
npm run test:coap-override
npm run test:dtls
npm run test:dtls-nosec
npm run test:protocol
```

The CoAP suite runs a real Zephyr CoAP service on 127.0.0.1 in the same
process and registers against it, so the assertions are made on the bytes the
server received: the method, the URI path, the query parameters, the content
format and the payload. The suite also covers the refusal of a server that
uses the Pre-Shared Key security mode: the plaintext binding never contacts
such a server in the clear.

`npm run test:coap` builds the shipped default configuration of the module,
with the CoAP option length the module sizes itself. `npm run test:coap-override`
builds the same sources with a larger CoAP option length, to show that an
application can still raise it.

The DTLS suite runs a real Zephyr DTLS server socket on 127.0.0.1 in the same
process and registers against it over DTLS 1.2, so the assertions are made on
the bytes the server received over the secured session. The suite also covers
the wrong pre-shared key and the unknown pre-shared key identity, which both
have to fail the handshake, the unsupported security modes, the missing
credentials, the port 5684, the default port of a server URI without a port
and the selection of the binding from the server URI scheme.

The DTLS suite covers the threading contract as well. It replaces the
Security object with an object that records the thread of the read, so the
test proves that the transport and the binding read the Security object in
the engine thread and not in the application thread.

The DTLS suite covers the plaintext binding as well: it proves that the
plaintext binding refuses a server that uses the Pre-Shared Key security
mode and that it honours the noSec development gate.
`npm run test:dtls-nosec` builds the same sources with the noSec
development gate open, to show that a NoSec server is then accepted and
served by the plaintext binding.
