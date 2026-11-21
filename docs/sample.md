# Native Simulator Sample

The sample is a minimal Sirena LwM2M client application. It provisions the
client, registers with a server, publishes telemetry through an observation,
handles a command from the server and deregisters. It is the behavior level
smoke of the public end-to-end flow: an application developer reads it to see
which call belongs where.

| Part | Path |
| --- | --- |
| Application | `src/samples/lwm2m_client/src/main.c` |
| Flow of the application | `src/samples/lwm2m_client/src/sample_flow.c` |
| Interface of the flow | `src/samples/lwm2m_client/src/sample_flow.h` |
| Build files of the application | `src/samples/lwm2m_client/CMakeLists.txt`, `src/samples/lwm2m_client/prj.conf` |
| End-to-end proof | `src/tests/sample/` |

## What the sample demonstrates

The flow of the sample is the sequence an application follows:

1. **Provision.** `sirena_lwm2m_provision()` fills the Security (0) instance
   with the server URI, the security mode, the credentials, the short server id
   and the lifetime, and the Server (1) instance with the same short server id
   and the lifetime.
2. **Identity.** The typed setters of `sirena/lwm2m/objects.h` fill the Device
   (3) instance: the manufacturer, the model number, the serial number and the
   firmware version.
3. **Endpoint name.** `sirena_lwm2m_transport_set_endpoint_name()` sets the LwM2M
   Endpoint Client Name, which no OMA object carries.
4. **Command handler.** `sirena_lwm2m_set_execute_handler()` registers the
   handler the protocol core calls when the server executes a resource. The
   handler of the sample answers every execute resource the client offers,
   among them Device 4 Reboot and Server 8 Registration Update Trigger.
5. **Transport.** `sirena_lwm2m_transport_start()` starts the binding that the
   server URI and the security mode select.
6. **Registration.** `sirena_lwm2m_transport_register()` registers the client.
   The registration carries the mandatory objects only.
7. **Telemetry.** The sample writes a value with a typed setter and publishes
   it with `sirena_lwm2m_notify_resource()`. The value reaches every observer
   of that resource path.
8. **Deregistration.** `sirena_lwm2m_transport_deregister()` deregisters the
   client on the way out.

The telemetry of the sample is the Device 9 Battery Level, which it lowers by
five on every report. The Device 9 resource belongs to the optional resources
of the Device object, so the sample enables
`CONFIG_SIRENA_LWM2M_OBJECT_DEVICE_OPTIONAL`. The v1 object registry accepts
the mandatory objects 0, 1 and 3 only, so an application cannot register an
object of its own yet. A real application replaces the simulated battery
level with the value of its own sensor, on a resource it owns.

The flow lives in `sample_flow.c` and not in `main.c`, so that the test suite
runs the very same code the application runs. `main.c` only reads the
configuration and calls the flow.

## Build and run

All commands run in Docker through the npm scripts:

```sh
npm run build:sample
npm run run:sample
```

`npm run build:sample` builds the sample for `native_sim` into `build/sample`.
`npm run run:sample` builds it and runs it. The process ends on its own, so the
command returns instead of hanging.

The sample registers with the server of its configuration. Without a server on
that address the registration fails, the sample reports the error in its log
and stops anyway. To see the whole flow, run an LwM2M server on the address of
the configuration, for example a Leshan server on `127.0.0.1:5683`, and run the
sample again.

## Configuration

The sample reads its configuration from Kconfig, so a build can be pointed at
another server without a change of the sources. The symbols live in the
`Sirena LwM2M sample` menu of the `Kconfig` of the module.

| Kconfig symbol | Purpose | Default |
| --- | --- | --- |
| `CONFIG_SIRENA_LWM2M_SAMPLE_SERVER_URI` | The URI of the LwM2M server. The scheme selects the binding: `coap`, `coaps`, `mqtt` or `mqtts`. | `coap://127.0.0.1:5683` |
| `CONFIG_SIRENA_LWM2M_SAMPLE_ENDPOINT_NAME` | The LwM2M Endpoint Client Name of the sample. | `sirena-sample` |
| `CONFIG_SIRENA_LWM2M_SAMPLE_SECURITY_MODE` | The security mode of the Security object: 0 pre-shared key, 1 public key, 2 certificate, 3 noSec, 4 certificate with the EST server. | 3 |
| `CONFIG_SIRENA_LWM2M_SAMPLE_IDENTITY` | The identity of the pre-shared key. | empty |
| `CONFIG_SIRENA_LWM2M_SAMPLE_SECRET_KEY` | The pre-shared key. | empty |
| `CONFIG_SIRENA_LWM2M_SAMPLE_SHORT_SERVER_ID` | The short server id of the Security and the Server instance. | 1 |
| `CONFIG_SIRENA_LWM2M_SAMPLE_LIFETIME` | The registration lifetime in seconds. | 300 |
| `CONFIG_SIRENA_LWM2M_SAMPLE_BINDING` | The binding mode of the registration. | `U` |
| `CONFIG_SIRENA_LWM2M_SAMPLE_MANUFACTURER` | The manufacturer of the Device object. | `Sirena` |
| `CONFIG_SIRENA_LWM2M_SAMPLE_MODEL_NUMBER` | The model number of the Device object. | `sirena-sample` |
| `CONFIG_SIRENA_LWM2M_SAMPLE_SERIAL_NUMBER` | The serial number of the Device object. | `sirena-sample-0001` |
| `CONFIG_SIRENA_LWM2M_SAMPLE_FIRMWARE_VERSION` | The firmware version of the Device object. | `1.0.0` |
| `CONFIG_SIRENA_LWM2M_SAMPLE_RUN_SECONDS` | The lifetime of the sample run in seconds. | 10 |
| `CONFIG_SIRENA_LWM2M_SAMPLE_TELEMETRY_INTERVAL_MS` | The interval between the telemetry reports in milliseconds. | 1000 |

A build that points the sample at another server overrides the symbols in its
`prj.conf` or on the command line of `west build`:

```sh
docker compose run --rm -T -w /opt/zephyr-workspace zephyr west build -b native_sim \
    /workspace/src/samples/lwm2m_client -d /workspace/build/sample \
    --extra-conf /workspace/src/samples/lwm2m_client/prj.conf \
    -DCONFIG_SIRENA_LWM2M_SAMPLE_SERVER_URI='"coaps://example.lw2m.com:5684"' \
    -DCONFIG_SIRENA_LWM2M_SAMPLE_SECURITY_MODE=0 \
    -DCONFIG_SIRENA_LWM2M_SAMPLE_IDENTITY='"sirena-sample"' \
    -DCONFIG_SIRENA_LWM2M_SAMPLE_SECRET_KEY='"0123456789abcdef"'
```

The noSec mode of the default configuration needs the development gate
`CONFIG_SIRENA_LWM2M_TRANSPORT_NOSEC`, which the `prj.conf` of the sample
opens. The pre-shared key mode needs the DTLS or the MQTT binding and the
credentials of the server.

## The bounded lifetime and the exit status

The sample runs for `CONFIG_SIRENA_LWM2M_SAMPLE_RUN_SECONDS` seconds, then it
deregisters and stops. It never idles forever: the native simulator keeps the
process alive after `main()` returns, so the sample ends the process itself on
the native simulator board and returns on every other board.

The exit status reports whether the sample could run: it is 0 when the sample
ran its bounded lifetime and stopped, and 1 when it could not start at all, for
example because the provisioning failed. The outcome of every step is in the
log and in the summary line the sample prints on the way out:

```text
sirena lwm2m sample: finished registered=1 reports=9 notifications=9 command=1
```

A registration that the server refuses is reported in the log and leaves
`registered=0` in the summary. The end-to-end proof below is what shows that
the flow itself works.

## The end-to-end proof

```sh
npm run test:sample
```

The suite of `src/tests/sample/` runs the flow of the sample against a local
in-process server, the same kind of peer the other suites use: a Zephyr CoAP
service that receives the registration and a Zephyr CoAP client that sends the
server initiated requests and receives the responses and the notifications. No
external server is needed.

The suite calls `sirena_lwm2m_sample_run()` with the configuration of the
in-process server, so it exercises the code the application runs and not a
copy of it.

| Assertion | What it proves |
| --- | --- |
| The peer receives the registration and its payload carries `</1/0>` and `</3/0>` and no `</0/0>` | The client registers and the registration holds the mandatory objects only. |
| The registration carries `ep=sirena-sample-test`, `lwm2m=1.2` and `lt=300` | The endpoint name, the version and the lifetime of the registration reach the server. |
| The peer observes `/3/0/9` and receives the notification of the sample | The telemetry of the sample reaches the observer of the server. |
| The notification is a confirmable message and carries a value the sample publishes | The notification follows the observation of RFC 7641 and carries the current value. |
| The peer executes `/3/0/4` and receives the changed answer | The command of the server reaches the client and the peer receives the answer. |
| The state of the sample reports the command | The execute handler of the sample ran for the command of the server. |
| The peer reads `/3/0/0` and receives the manufacturer of the sample | A remote read reaches the resources of the application. |
| The peer writes `/1/0/1` and reads the new value back | A remote write reaches the client and the value is stored. |
| The peer receives the deregistration and the sample thread ends on its own | The sample shuts down cleanly and within its bounded lifetime. |

## The other suites

The sample does not replace the suites of the modules. They stay the proof of
the behavior of the modules themselves:

```sh
npm run test:registry
npm run test:codec
npm run test:objects
npm run test:objects-optional
npm run test:coap
npm run test:coap-override
npm run test:dtls
npm run test:dtls-nosec
npm run test:protocol
npm run test:mqtt
npm run test:sample
```
