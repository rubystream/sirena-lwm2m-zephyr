# The Conformance Harness: Leshan and Sirena

The harness runs the real client against a real LwM2M server and checks, from
the point of view of the server, that the client registers, publishes telemetry
and answers the commands of the server.

It runs in two tiers, because no single server covers the whole LwM2M 1.2
wire format.

| Tier | Server | The client advertises | The server shows |
| --- | --- | --- | --- |
| 1 | Leshan `2.0.0-M18` | `lwm2m=1.1` | `1.1` |
| 2 | `sirena-lwm2m2-server` | `lwm2m=1.2` | `1.2` |

| Part | Path |
| --- | --- |
| Leshan server image | `src/tests/leshan/leshan.Dockerfile` |
| Sirena server image | `src/tests/sirena/sirena-lwm2m2.Dockerfile` |
| Client under test | `src/tests/conformance/src/test_conformance.c` |
| Orchestration of the run | `src/tests/conformance/harness.py` |
| The adapter of a server | `src/tests/conformance/servers/` |
| Build files of the client | `src/tests/conformance/CMakeLists.txt`, `src/tests/conformance/prj.conf` |
| Configuration symbols of the harness | `src/tests/conformance/Kconfig.conformance` |
| The version of a tier | `src/tests/conformance/prj-leshan.conf`, `src/tests/conformance/prj-sirena.conf` |
| Services of the run | `compose.yml` |
| Host entry point | `scripts/test-conformance.sh` |

## Why the matrix has two tiers

**Leshan knows the LwM2M versions `1.0` and `1.1` only.** Its `LwM2mVersion`
enum has no other value in any release, and Leshan `2.0.0-M18` refuses a
registration that advertises `lwm2m=1.2` with `4.15 Unsupported
Content-Format` and the payload `unsupported lwm2m version`. The check was
added to Leshan in `M18`, so the newest Leshan is also the strictest one.

A conformance oracle may not be lenient: a server that tolerates a version
string it does not implement proves nothing about that version. The Leshan tier
therefore verifies the client up to the version Leshan really implements,
`lwm2m=1.1`, against the newest Leshan release, `2.0.0-M18`. The client
announces the version it is asked to announce through the Kconfig symbol
`CONFIG_SIRENA_LWM2M_LWM2M_VERSION`, so the tier picks it without a patch of
the source.

The Sirena server implements the LwM2M 1.2 line, so the second tier verifies
`lwm2m=1.2` against it. The Sirena server is strict about the wire format as
well: it rejects a value it cannot decode instead of taking it, which is what
makes it the oracle of the 1.2 tier.

The client keeps `1.2` as the shipped default of
`CONFIG_SIRENA_LWM2M_LWM2M_VERSION`, because the client is an LwM2M 1.2
client. Only the tiers choose another value.

## The pinned servers

### Leshan `2.0.0-M18`

The image runs the self contained `leshan-demo-server` artifact of Maven
Central, so it needs no other artifact and the host needs no Java. The
`src/tests/leshan/leshan.Dockerfile` keeps `2.0.0-M18` as the default of its
own build argument and checks the SHA-256 sum of the jar. The sum is the
guard: a jar that does not match the pinned sum stops the build instead of
changing the server silently.

The version is named in `compose.yml`, in the tag of the image and in the
build argument of the `leshan` service.

### `sirena-lwm2m2-server`

There is no published image of the Sirena server on Docker Hub, ghcr.io or
quay.io, so the harness builds it. The image is built from the published npm
package `@sirena-lwm2m/server` `0.5.0`, which ships the compiled `dist/` in
its tarball, so a runtime-only image from the tarball is the same server the
upstream project builds from its private source repository.

The `src/tests/sirena/sirena-lwm2m2.Dockerfile` downloads the tarball of that
exact version from the npm registry and checks it twice before it installs it:
against the `shasum` of the registry and against its `integrity` value, which
is the SHA-512 sum of the tarball. A tarball that does not match stops the
build. The image runs the server with its in-memory store on the CoAP port
`5683` and on the HTTP port `8080`, which is the default of the upstream
Dockerfile.

The server prints a warning on its standard error when it binds to a
non-loopback address with the in-memory store. The warning is expected and
does not fail the run.

## What the harness proves

Both tiers assert the same behaviours, and they assert them from the point of
view of the server:

1. **Registration.** The server shows the endpoint with the parameters the
   client claims: the endpoint name `sirena-conformance`, the lifetime `300`,
   the LwM2M version of the tier, the binding mode `U` and the objects `1`
   and `3` the client registers.
2. **Telemetry.** The server observes the Device 9 Battery Level resource of
   the client and receives the notifications of it with the value the client
   publishes. The harness checks the type of the value as well, so a server
   that could not decode it fails the run.
3. **Read.** The server reads the Device 9 resource and gets the value the
   client published.
4. **Write.** The server writes the Device 14 UTC offset resource and the
   client answers. The server reads the resource back and gets the value it
   wrote.
5. **Execute.** The server executes the Device 4 Reboot resource and the
   client answers.
6. **Deregistration.** The server reports the deregistration of the client
   and the client stops with the exit code `0`.

The client under test asserts the same flow from its own side: it registers,
it publishes telemetry to an observer, it keeps the value the server wrote and
it receives the command of the server. Both sides fail the run when they do
not see what they expect.

## The two servers do not share an API

The two servers expose different REST APIs, so the harness drives each of them
through its own adapter in `src/tests/conformance/servers/`.

| | Leshan | Sirena |
| --- | --- | --- |
| The client is addressed by | the endpoint name | the server assigned id, for example `c1` |
| The registration document | `endpoint`, `lifetime`, `lwM2mVersion`, `bindingMode`, `objectLinks` | `endpointName`, `lifetimeSeconds`, `enablerVersion`, `bindingMode`, `objectList` |
| The observations | an event stream on `/api/event` | `GET /api/clients/:id/observations/:observationId/notifications` |
| A read | `GET /api/clients/{endpoint}{path}` | `POST /api/clients/:id/read` |
| A write | `PUT /api/clients/{endpoint}{path}` | `POST /api/clients/:id/write` |
| An execute | `POST /api/clients/{endpoint}{path}` | `POST /api/clients/:id/execute` |

The adapter of a server hides those differences from the flow of the harness,
which is the same for both tiers.

## Running the harness

```sh
npm run test:conformance
```

The script runs both tiers and reports which tier failed. A single tier runs
with:

```sh
npm run test:conformance:leshan
npm run test:conformance:sirena
```

The script exits with a non-zero status when an assertion of the harness or of
the client fails, so it can be used as a gate in a pipeline. The run is
repeatable: it can be started again right after it finished, and it leaves no
TAP device, no container and no port behind.

`npm run test:leshan` is the name the harness had before the matrix was split
in two. It runs the Leshan tier and is kept so that the pipelines that call it
keep working.

The host needs Docker and the `/dev/net/tun` device. It needs no Java, no
Node.js for the server, no TAP device of its own and no privileges: the device
and the capability live in the container.

A fresh checkout has no west workspace yet, so the first run creates it and
clones the whole Zephyr tree into it, which takes a while. The later runs
reuse that workspace and only rebuild the client.

## How the run is wired

The native simulator has no network of its own. It reaches the host through a
TAP device, which is the only way to put the Zephyr network stack on the wire
against a server that is not part of the process.

1. The `leshan` service of `compose.yml` runs the demo server of Leshan
   `2.0.0-M18`. It listens for CoAP on the port `5683` and serves its REST API
   on the port `8080`. The `sirena-lwm2m2` service runs the Sirena server on
   the same two ports.
2. The `leshan-client` service shares the network namespace of the `leshan`
   service, the `sirena-client` service the one of the `sirena-lwm2m2`
   service, so both see the same addresses.
3. The client under test is a native simulator build with
   `CONFIG_ETH_NATIVE_TAP=y`. The driver of Zephyr creates the TAP device
   `zeth` itself and the harness never creates it, because a second
   `TUNSETIFF` for the same name fails with `EBUSY`.
4. The harness waits for the device `zeth` to appear, gives its Linux side the
   address `192.168.0.1/24` and brings it up. The Zephyr side takes the
   address `192.168.0.2/24` from `CONFIG_NET_CONFIG_MY_IPV4_ADDR`.
5. The client registers with `coap://192.168.0.1:5683`. The registration
   reaches the server through the TAP device, and the answer of the server
   travels back the same way.
6. The harness drives the server through its REST API: it observes the
   resource, it reads it, it writes it and it executes it. It watches the
   events of the server for the notification and the deregistration.

The client and the harness run at the same time: the device `zeth` only exists
while the client runs, and the client retries its registration until the
harness has given the Linux side of the device an address.

## The clock of the native simulator

The native simulator advances its clock as fast as it can when every thread
sleeps. That is the right behavior for a test that runs against an in-process
peer, and the wrong behavior for a client that waits for a real network
answer: the client would give up on the CoAP retransmission before the answer
of the server arrives.

The harness therefore starts the client with the `--rt` switch of the native
simulator, which ties the clock of the simulator to the clock of the host. The
timeouts of the client then mean what they mean on a device.

## The harness and the client code

The client under test uses the public API of the module, the same way an
application does: it provisions the client, it starts the transport, it
registers, it publishes telemetry with `sirena_lwm2m_notify_resource()` and it
registers an execute handler with `sirena_lwm2m_set_execute_handler()`. The
harness does not mock the client, it does not replace the transport and it does
not touch the wire format of the client.

The `Kconfig.conformance` file holds the values both sides agree on: the
server URI, the endpoint name, the lifetime, the binding mode, the telemetry
value and the value the server writes. The client is built from them, and the
harness reads them from the resolved configuration of that build, the `.config`
file of the build directory, so the harness always sees the values the client
was built with and the client and the harness cannot drift apart.

The version the client announces is the module symbol
`CONFIG_SIRENA_LWM2M_LWM2M_VERSION`, which the file of the tier overrides:
`prj-leshan.conf` sets it to `1.1` and `prj-sirena.conf` sets it to `1.2`.
