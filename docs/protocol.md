# Protocol Core

The protocol core answers the requests the LwM2M server sends to the client. It
implements the Device Management and Service Enablement interface of OMA LwM2M
1.2, section 6.4.5, for the operations that a v1 client needs, and the
Information Reporting interface with CoAP Observation, RFC 7641.

| Part | Header | Implementation |
| --- | --- | --- |
| Protocol interface | `src/include/sirena/lwm2m/protocol.h` | `src/lib/sirena_lwm2m_protocol.c` |
| CoAP endpoint | `src/lib/sirena_lwm2m_coap_endpoint.h` | `src/lib/sirena_lwm2m_coap_endpoint.c` |

The protocol core is transport neutral. It resolves a resource path against the
object registry, negotiates the content format and reads, writes or executes the
resource. It never touches a CoAP type. The CoAP endpoint carries the wire
format, the observation state and the notifications, and calls the protocol core
for every request it receives.

## The operations

| Operation | Method | URI | Success | Failure |
| --- | --- | --- | --- | --- |
| Read | GET | `/{ObjectID}/{ObjectInstanceID}/{ResourceID}` | 2.05 Content | 4.00, 4.04, 4.05, 4.06 |
| Write | PUT | `/{ObjectID}/{ObjectInstanceID}/{ResourceID}` | 2.04 Changed | 4.00, 4.04, 4.05, 4.15 |
| Execute | POST | `/{ObjectID}/{ObjectInstanceID}/{ResourceID}` | 2.04 Changed | 4.00, 4.04, 4.05 |
| Write-Attributes | PUT (with URI-Query) | `/{ObjectID}[/{ObjectInstanceID}][/{ResourceID}]` | 2.04 Changed | 4.00, 4.04 |
| Discover | GET (Accept: application/link-format) | `/{ObjectID}[/{ObjectInstanceID}][/{ResourceID}]` | 2.05 Content | 4.04, 4.06, 5.00 |
| Read-Composite | FETCH | SenML JSON/CBOR target list | 2.05 Content | 4.00, 4.04, 4.05, 4.06, 4.15, 5.00 |
| Write-Composite | iPATCH | SenML JSON/CBOR target and value list | 2.04 Changed | 4.00, 4.04, 4.15, 5.00 |
| Observe | GET with Observe = 0 | `/{ObjectID}/{ObjectInstanceID}/{ResourceID}` | 2.05 Content | 4.00, 4.04, 4.05, 4.06, 5.03 |
| Cancel Observation | GET with Observe = 1 | `/{ObjectID}/{ObjectInstanceID}/{ResourceID}` | 2.05 Content | 4.00, 4.04, 4.05 |
| Notify | notification | the observed resource | 2.05 Content | |
| Send | POST | /dp?ep=<Endpoint> | 2.04 Changed | 4.00, 4.04, 4.05, 4.06 |

The status codes of the table are the CoAP codes of the response. The endpoint
maps them to the LwM2M operations:

| Code | Meaning |
| --- | --- |
| 2.04 Changed | The write or the execute succeeded. |
| 2.05 Content | The read succeeded and the payload carries the value. |
| 4.00 Bad Request | The payload does not match the resource, or the write payload cannot be decoded. |
| 4.04 Not Found | The path does not resolve to a resource of the registry. |
| 4.05 Method Not Allowed | The resource does not support the operation. A read of a resource that only executes, a write of a read-only resource and an execute of a resource that only reads are refused with this code. |
| 4.06 Not Acceptable | The server named no content format the client can encode the resource in, or it named more of them than the client can hold. |
| 4.15 Unsupported Content-Format | The payload of a write uses a content format the codec cannot decode. The codec decodes all formats it can encode. |
| 5.00 Internal Server Error | The client could not build the representation of the resource, for example because the encoded value does not fit the configured buffer. |
| 5.03 Service Unavailable | The observation capacity of the client is exhausted. |

The access mode of the registry decides what a resource allows. A resource that
the registry marks as read-only is readable over the wire and refuses a write,
and a resource that it marks as executable is neither readable nor writable.

## Content format negotiation

The server tells the client which encoding it accepts with the CoAP Accept
option. The client picks one of the formats it supports with
`sirena_lwm2m_content_format_negotiate()` and uses it for the response. A
format the codec cannot encode for the type of the resource is not acceptable
for that request, so the negotiation takes the type of the resource into
account.

| Rule | Behaviour |
| --- | --- |
| The request carries an Accept option | The client picks the first format of the list that it supports and that the codec can encode the resource in. The list is in the order of the preference of the server, as RFC 7251 asks. |
| The request carries no Accept option | The client uses its preferred format, `CONFIG_SIRENA_LWM2M_PREFERRED_CONTENT_FORMAT`, when the codec can encode the resource in it. Otherwise it uses the first format it supports that the codec can encode the resource in. |
| No format of the Accept list is supported, or none of them encodes the resource | The client answers 4.06 Not Acceptable. |
| The Accept list is longer than `CONFIG_SIRENA_LWM2M_ACCEPTED_FORMATS_MAX` | The client answers 4.06 Not Acceptable. It never ignores the formats it cannot hold. |
| The request is a write | The payload is decoded with the Content-Format of the request. The TLV format carries no type of its own, so the type of the value comes from the object model of the resource the request addresses. Without a Content-Format option the preferred format is used. The Accept option of a write is ignored: the response of a write carries no representation, so there is nothing for the server to constrain. |
| The request is an execute | The payload is passed to the execute handler as it is. The Accept option of an execute is ignored, for the same reason. |

The preferred format defaults to the TLV format, because the codec encodes every
resource type in it. The plain text format only encodes string resources and
the opaque format only encodes opaque resources, so a read of another resource
type with one of those formats is refused with 4.06 Not Acceptable.

The client never negotiates a format that the codec cannot encode for the
resource. A format the codec cannot decode is refused with 4.15 Unsupported
Content-Format instead of being parsed as something else.

The response that carries a representation carries the Content-Format option of
the negotiated format, also when the format is the plain text format, whose
identifier is zero. A response that carries no representation carries no
Content-Format option.

The formats the codec knows are the identifiers of `sirena/lwm2m/codec.h`: plain
text, TLV, JSON, CBOR, SenML JSON, SenML CBOR, SenML ETCH JSON, SenML ETCH CBOR
and opaque. The codec encodes all of them and decodes every format it can encode.

## The observations

An observation is a CoAP Observe registration on a resource path.

| Step | Behaviour |
| --- | --- |
| Register | A GET with the Observe option set to 0 registers an observation. The response carries the current value and the Observe option with the sequence number of the observation. |
| Notify | `sirena_lwm2m_notify_resource()` reads the current value of the resource and sends a notification to every observation of that path. The notification is a confirmable message, as RFC 7641 asks, so that the server acknowledges it and a loss is visible. It carries the observation token, the Observe option with the next sequence number and the value in the format the observation negotiated. |
| Cancel | A GET with the Observe option set to 1 and the token of the observation removes it, as RFC 7641 section 3.6 asks. The client answers with the current value. The state of the cancelled observation is released, so it never leaks and it is never notified again. |
| Capacity | The client holds at most `CONFIG_SIRENA_LWM2M_MAX_OBSERVATIONS` observations. A registration beyond that capacity is refused with 5.03 Service Unavailable, never dropped silently. |

The observations live in static storage. The client never uses the heap for
them. The client does not retransmit a notification it did not get an
acknowledgement for.

The MQTT binding keeps its own observations, as the CoAP endpoint keeps its
own, because the wire format of an observation is the one of the binding: the
CoAP endpoint registers an observation with the Observe option of a GET, the
MQTT binding registers one with the Observe operation of the information
reporting interface and removes it with the Cancel-Observe operation. The
notification entry point is the same for both, so the application publishes a
value the same way whatever the binding is.

## The application interface

```c
int sirena_lwm2m_notify_resource(uint16_t object_id, uint16_t instance_id,
                                 uint16_t resource_id);
```

The call publishes the current value of a resource to the observations of that
path. It queues the work on the engine thread and waits for its completion, so
it returns the result of the work to the caller. It returns the number of the
observations that were notified, so `0` means that no observation of that path
was registered. It returns `-ENOTSUP` when no transport is bound or when the
engine thread does not run.

The application publishes the value with `sirena_lwm2m_set_value()` of
`sirena/lwm2m/objects.h` before it calls `sirena_lwm2m_notify_resource()`. The
setter applies the write in engine context, so the value the client reads from
the registry when it builds the notification is the value the application
published.

The execute handler of the application is registered with
`sirena_lwm2m_set_execute_handler()`. The protocol core calls it when the server
executes a resource.

## The threading contract

Every protocol operation runs in engine context. The engine thread owns the
socket of the transport, polls it and dispatches the requests it receives. A
request is therefore answered in the engine thread, never in the thread of the
CoAP stack and never in the thread of the application.

The application calls the transport API and the notification API. Both only
queue their work on the engine thread and wait for its completion.

The object registry and the built-in objects API are not thread-safe. The
application must not touch the registry while the transport runs. It publishes
the values it observes with `sirena_lwm2m_set_value()` of
`sirena/lwm2m/objects.h`, which applies the write in engine context.

## The socket ownership

Zephyr's CoAP client is response only. It dispatches an incoming datagram by
the token of a request the client itself sent and answers a request with no
matching token with a reset. It also owns the thread that polls the socket. A
server initiated request therefore never reaches an application that uses it.

The module therefore owns the receive path of the transport socket itself. The
CoAP endpoint creates the request, sends it, retransmits it and matches the
response, and it parses the requests the server sends and dispatches them into
the protocol core. The registration, the update and the deregistration keep
their behaviour and their paths, and the server reaches the device management
interface on the same socket the client registered on, as OMA LwM2M 1.2 requires.

A response is only matched when it comes from the server the request was sent
to, so a datagram of another peer never completes an exchange. A request whose
path holds more segments than a resource path has is refused with 4.04 Not
Found: the endpoint never copies more segments into the path than it can hold.

The engine thread polls the socket with the interval of
`CONFIG_SIRENA_LWM2M_ENGINE_POLL_INTERVAL_MS`. The interval bounds the latency
of a server request and the resolution of the retransmission timers.

## Error codes

| Code | Meaning |
| --- | --- |
| `0` | The request succeeded. |
| `-EINVAL` | The path does not resolve to a resource, the payload does not match the resource type, or the arguments are invalid. |
| `-ENOENT` | The object, the instance or the resource does not exist. |
| `-ENOTSUP` | The resource does not support the operation, the content format is not supported, the codec cannot decode the format of a write payload, or the engine thread does not run. |
| `-ENOMEM` | The encoded payload does not fit the configured buffer. |
| `-ETIMEDOUT` | The server did not answer the request within `CONFIG_SIRENA_LWM2M_COAP_RESPONSE_TIMEOUT_MS`. |
| `-EAGAIN` | The engine request queue is full. |
| `-EBADMSG` | The server answered 4.00 Bad Request. |
| `-EACCES` | The server answered 4.03 Forbidden. |
| `-EPROTO` | The server answered 4.12 Precondition Failed, or it accepted a registration without a location path. |
| `-EHOSTUNREACH` | The server answered 5.02 Bad Gateway. |
| `-ECONNRESET` | The server answered the request with a reset. |
| `-EIO` | The server answered any other status code. |

The status codes of the responses are listed with the operations above. The
error codes of the API are the negative `errno` values of the table.

## Configuration

| Kconfig symbol | Purpose | Default |
| --- | --- | --- |
| `CONFIG_SIRENA_LWM2M_MAX_OBSERVATIONS` | Number of observations the client holds at the same time. | 8 |
| `CONFIG_SIRENA_LWM2M_PREFERRED_CONTENT_FORMAT` | Content format the client uses when the server names none. | 11542, the TLV format |
| `CONFIG_SIRENA_LWM2M_PROTOCOL_PAYLOAD_MAX` | Size of the buffer that holds an encoded resource value. | 256 |
| `CONFIG_SIRENA_LWM2M_PROTOCOL_VALUE_MAX` | Size of the buffer that holds a raw resource value. | 64 |
| `CONFIG_SIRENA_LWM2M_ACCEPTED_FORMATS_MAX` | Number of acceptable content formats the client reads from one request. | 4 |
| `CONFIG_SIRENA_LWM2M_ENGINE_POLL_INTERVAL_MS` | Interval the engine thread polls the transport socket with. | 20 |
| `CONFIG_SIRENA_LWM2M_COAP_MESSAGE_MAX` | Size of the buffer that holds a CoAP message. | 512 |
| `CONFIG_SIRENA_LWM2M_LWM2M_VERSION` | The LwM2M version the client announces in the registration. | 1.2 |

## Not in this slice

- The composite operations: Write-Composite and Observe-Composite.
- The creation and the deletion of object instances.
- The block-wise transfer of large payloads.
- The decoding of the structured content formats. The codec encodes them, but a
  write in one of them is refused with 4.15. A read composite parses the SenML
  JSON and SenML CBOR target lists only.

## Tests

```sh
npm run test:protocol
```

The suite runs a real CoAP peer on 127.0.0.1 in the same process. The peer is a
Zephyr CoAP service that receives the registration and a Zephyr CoAP client that
sends the server initiated requests and receives the responses and the
notifications, so the assertions are made on the bytes the client sent back:
the response code, the content format, the observe option, the token and the
payload.

The suite covers the read, the write and the execute with their status codes,
the content format negotiation with and without an Accept option, the refusal
of a format the codec cannot encode for the resource, the refusal of an Accept
list the client cannot hold, a path with too many segments, the observation
with its notification, the capacity of the observation table and the
cancellation of an observation.
