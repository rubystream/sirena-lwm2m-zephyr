# Transport Bindings

The transport layer connects the LwM2M client core to a server. It is split in
three parts:

| Part | Header | Implementation |
| --- | --- | --- |
| Transport interface | `src/include/sirena/lwm2m/transport.h` | `src/lib/sirena_lwm2m_transport.c` |
| Engine thread | `src/include/sirena/lwm2m/engine.h` | `src/lib/sirena_lwm2m_engine.c` |
| CoAP/UDP binding | `src/include/sirena/lwm2m/coap.h` | `src/lib/sirena_lwm2m_coap.c` |

The transport interface is what the reusable core talks to. It carries the
registration lifecycle only and never exposes a CoAP type to the core, so the
binding can be replaced without touching the core.

## Transport interface

```c
struct sirena_lwm2m_transport_ops {
    int (*register_client)(void);
    int (*update)(void);
    int (*deregister)(void);
};

int sirena_lwm2m_transport_bind(const struct sirena_lwm2m_transport_ops *ops);
int sirena_lwm2m_transport_register(void);
int sirena_lwm2m_transport_update(void);
int sirena_lwm2m_transport_deregister(void);
```

`sirena_lwm2m_transport_bind()` attaches a binding to the transport interface
and starts the engine thread. The three lifecycle calls queue the request on
the engine thread and wait for its completion, so they return the result of
the transport work to the caller.

A binding that is not bound yet, or a build without the CoAP binding, returns
`-ENOTSUP` from the lifecycle calls.

## Engine thread

The engine owns the transport. It runs one dedicated thread with a bounded
static request queue:

- The queue holds `CONFIG_SIRENA_LWM2M_ENGINE_QUEUE_SIZE` requests. There is no
  heap use: the queue and its entries are static.
- `sirena_lwm2m_engine_submit()` reserves a free queue entry, fills it, queues
  it and waits on the entry semaphore for the engine to complete it. Requests
  are processed in the order they were queued.
- When every queue entry is in use the submit returns `-EAGAIN`.
- The engine thread runs the transport work, so the binding is never called
  re-entrantly from an application thread.

The stack size and the priority of the engine thread are configurable with
`CONFIG_SIRENA_LWM2M_ENGINE_STACK_SIZE` and `CONFIG_SIRENA_LWM2M_ENGINE_PRIORITY`.

### Threading contract

The application calls the transport API; the engine thread performs the work.
The CoAP response callback of the Zephyr CoAP client runs in the CoAP client
thread. It only copies the response into a static exchange buffer and signals
the engine thread; every transport decision, including the interpretation of
the response code and the storage of the location path, is taken in the engine
thread.

The object registry and the built-in objects API are not thread-safe. The
binding reads the Security and Server objects from the engine thread, so the
application must finish provisioning and configuring the client before it
starts the transport.

## CoAP/UDP binding

The binding implements the LwM2M 1.2 registration interface over plain CoAP
over UDP.

```c
int sirena_lwm2m_coap_set_endpoint_name(const char *name);
int sirena_lwm2m_coap_start(void);
int sirena_lwm2m_coap_get_location_path(char *buffer, size_t *length);
```

- `sirena_lwm2m_coap_set_endpoint_name()` sets the LwM2M Endpoint Client Name.
  The name is not carried by any OMA object, so the application must supply it.
  A `NULL` name is rejected with `-EINVAL`, an empty name clears the endpoint
  name, and a name longer than `CONFIG_SIRENA_LWM2M_ENDPOINT_NAME_MAX` is
  rejected with `-EINVAL`. Registering without an endpoint name fails with
  `-EINVAL` and no request is sent.
- `sirena_lwm2m_coap_start()` creates the UDP socket, initializes the CoAP
  client and binds the binding to the transport interface, which starts the
  engine thread. It is idempotent and returns `0` when the transport is
  already running.
- `sirena_lwm2m_coap_get_location_path()` returns the location path the server
  assigned to the registration, for example `/rd/1`. It returns the character
  count without the terminating NUL, like the other string getters of the
  project.

### Where the registration parameters come from

| Parameter | Source |
| --- | --- |
| Server address | Security object resource 0, the LwM2M Server URI, for example `coap://192.0.2.10:5683` |
| Lifetime | Server object instance 0, resource 1 |
| Binding mode | `U`, hard-coded for this binding |
| LwM2M version | `1.2`, hard-coded for this binding |
| Endpoint name | The application, through `sirena_lwm2m_coap_set_endpoint_name()` |
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
(id 3) objects are part of it.

The Update request carries the lifetime and the binding mode. It never carries
the endpoint name.

The server answers a successful registration with a `2.01 Created` response
that carries a Location-Path option. The binding stores the location path and
uses it for the Update and the Deregister requests. The LwM2M 1.0 style
Location option, CoAP option 7, is accepted as well when the Location-Path
option is absent. A successful response without a location path is rejected
with `-EPROTO`, because the Update and the Deregister requests could not be
addressed.

The Deregister request clears the stored location path. A Deregister or an
Update without a prior registration fails with `-EINVAL`.

### Error codes

| Code | Meaning |
| --- | --- |
| `0` | The server accepted the request. |
| `-EINVAL` | The endpoint name is missing or invalid, the server URI is malformed, no registration exists for Update or Deregister, or the request path does not fit the CoAP path buffer. |
| `-EALREADY` | The transport was not started. |
| `-EAGAIN` | The engine request queue is full. |
| `-ENOTSUP` | The server URI uses the `coaps://` scheme, the host is not a numeric IPv4 address, or no binding is bound to the transport interface. |
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

## Configuration

| Kconfig symbol | Purpose | Default |
| --- | --- | --- |
| `CONFIG_SIRENA_LWM2M_TRANSPORT_COAP` | Enables the CoAP/UDP binding. It depends on `CONFIG_COAP` and `CONFIG_COAP_CLIENT` and selects `CONFIG_COAP_EXTENDED_OPTIONS_LEN`. | `y` |
| `CONFIG_SIRENA_LWM2M_ENGINE_QUEUE_SIZE` | Number of queued engine requests. | 8 |
| `CONFIG_SIRENA_LWM2M_ENGINE_STACK_SIZE` | Stack size of the engine thread. | 2048 |
| `CONFIG_SIRENA_LWM2M_ENGINE_PRIORITY` | Priority of the engine thread. | 5 |
| `CONFIG_SIRENA_LWM2M_ENDPOINT_NAME_MAX` | Maximum length of the endpoint client name. | 32 |
| `CONFIG_SIRENA_LWM2M_LOCATION_PATH_MAX` | Maximum length of the stored location path. | 64 |
| `CONFIG_SIRENA_LWM2M_REGISTRATION_PAYLOAD_MAX` | Maximum length of the registration payload. | 256 |
| `CONFIG_SIRENA_LWM2M_COAP_RESPONSE_TIMEOUT_MS` | Time the binding waits for a server response. | 10000 |

The registration request carries the endpoint name in a URI-Query option. Zephyr
limits the length of an option value to `CONFIG_COAP_EXTENDED_OPTIONS_LEN_VALUE`
bytes, 13 by default. An endpoint name longer than 9 characters therefore needs
a larger value:

```conf
CONFIG_COAP_EXTENDED_OPTIONS_LEN=y
CONFIG_COAP_EXTENDED_OPTIONS_LEN_VALUE=64
```

## Not in this binding

- DTLS and the `coaps://` scheme. The DTLS binding is a separate slice.
- Observation, notification and the read, write and execute operations.
- Content format negotiation on the registration payload.
- The periodic re-registration timer that drives the Update. The Update is
  implemented and reachable through the API, the scheduling policy belongs to
  the protocol layer.
- Host names and IPv6 addresses in the server URI.

## Tests

```sh
npm run test:coap
```

The suite runs a real Zephyr CoAP service on 127.0.0.1 in the same process and
registers against it, so the assertions are made on the bytes the server
received: the method, the URI path, the query parameters, the content format
and the payload.
