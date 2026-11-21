# Leshan Conformance Harness

The harness runs the real client against a real Leshan server and checks, from
the point of view of the server, that the client registers, publishes telemetry
and answers the commands of the server. It is the conformance oracle of the
epic: Leshan is the reference implementation of the LwM2M server, so a wire
behavior that Leshan accepts is a wire behavior that works.

| Part | Path |
| --- | --- |
| Leshan server image | `src/tests/leshan/leshan.Dockerfile` |
| Client under test | `src/tests/leshan/src/test_leshan.c` |
| Orchestration of the run | `src/tests/leshan/harness.py` |
| Build files of the client | `src/tests/leshan/CMakeLists.txt`, `src/tests/leshan/prj.conf` |
| Configuration symbols of the harness | `src/tests/leshan/Kconfig.leshan` |
| Services of the run | `compose.yml` |
| Host entry point | `scripts/test-leshan.sh` |

## The version constraint

**The harness is pinned to Leshan `2.0.0-M17` and must stay on it.**

Leshan `2.0.0-M18` refuses a registration that advertises `lwm2m=1.2` and
answers `4.15 Unsupported Content-Format` with the payload `unsupported lwm2m
version`. The check was added to Leshan in `M18`, and the `LwM2mVersion` enum of
Leshan knows `1.0` and `1.1` only, in every release. Leshan `2.0.0-M17` accepts
the registration of a LwM2M 1.2 client and answers `2.01 Created`.

The client of this repository sends `lwm2m=1.2`, which the OMA LwM2M Core 1.2
specification allows. The limitation is on the server side, so the harness picks
the server instead of changing the client. Do not raise the version of the
Leshan image without checking the registration again: a newer Leshan answers
the registration with an error and the harness fails at its first step.

The version is named in `compose.yml`, in the tag of the image and in the
build argument of the `leshan` service, which is the version of the jar the
image is built from. The `src/tests/leshan/leshan.Dockerfile` keeps
`2.0.0-M17` as the default of its own build argument, so a direct
`docker build` works on its own, downloads the jar of that version from Maven
Central and checks its SHA-256 sum. The sum is the guard: a jar that does not
match the pinned sum stops the build instead of changing the server silently.
The jar is the self contained `leshan-demo-server` artifact, so the image needs
no other artifact and the host needs no Java.

## What the harness proves

The harness asserts the following, all of them from the server side:

1. **Registration.** The Leshan server shows the endpoint with the parameters
   the client claims: the endpoint name `sirena-leshan`, the lifetime `300`,
   the LwM2M version `1.2`, the binding mode `U`, the root path `/` and the
   object links `/1/0` and `/3/0`.
2. **Telemetry.** The Leshan server observes the Device 9 Battery Level
   resource of the client and receives the notifications of it with the value
   the client publishes.
3. **Read.** The Leshan server reads the Device 9 resource and gets the value
   the client published.
4. **Write.** The Leshan server writes the Device 14 UTC offset resource and
   the client answers. The server reads the resource back and gets the value it
   wrote.
5. **Execute.** The Leshan server executes the Device 4 Reboot resource and the
   client answers.
6. **Deregistration.** The Leshan server reports the deregistration of the
   client and the client stops with the exit code `0`.

The client under test asserts the same flow from its own side: it registers,
it publishes telemetry to an observer, it keeps the value the server wrote and
it receives the command of the server. Both sides fail the run when they do
not see what they expect.

## Running the harness

```sh
npm run test:leshan
```

The script exits with a non-zero status when an assertion of the harness or of
the client fails, so it can be used as a gate in a pipeline. The run is
repeatable: it can be started again right after it finished, and it leaves no
TAP device, no container and no port behind.

The host needs Docker and the `/dev/net/tun` device. It needs no Java, no TAP
device of its own and no privileges: the device and the capability live in the
container.

A fresh checkout has no west workspace yet, so the first run creates it and
clones the whole Zephyr tree into it, which takes a while. The later runs
reuse that workspace and only rebuild the client.

## How the run is wired

The native simulator has no network of its own. It reaches the host through a
TAP device, which is the only way to put the Zephyr network stack on the wire
against a server that is not part of the process.

1. The `leshan` service of `compose.yml` runs the demo server of Leshan
   `2.0.0-M17`. It listens for CoAP on the port `5683` and serves its REST API
   on the port `8080`.
2. The `leshan-client` service shares the network namespace of the `leshan`
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
   server event stream of the REST API to see the notification and the
   deregistration.

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

The `Kconfig.leshan` file holds the values both sides agree on: the server
URI, the endpoint name, the lifetime, the version, the binding mode, the
telemetry value and the value the server writes. The client is built from
them, and the harness reads them from the resolved configuration of that
build, the `.config` file of the build directory, so the harness always sees
the values the client was built with and the client and the harness cannot
drift apart.
