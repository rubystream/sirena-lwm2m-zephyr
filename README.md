# sirena-lwm2m-zephyr

> The native Zephyr SDK for building, integrating, and running Sirena LwM2M
> clients on embedded devices.

## Project Intent

Give small embedded devices a native, resource-predictable voice in the Sirena LwM2M
platform. v1 ships a LwM2M 1.2 client for Zephyr — CoAP/UDP+DTLS plus MQTT — keeping
only Security/Server/Device mandatory and everything else opt-in, with static allocation
and smallest-device budgets as a hard guardrail. We port the client natively rather than
wrap Zephyr’s older built-in stack, to keep wire parity with Sirena. We do not trade the
smallest-device budget for feature breadth, do not build a server or gateway, and do not
starve application work with heavyweight management. Protocol version, mandated objects,
and allocation policy are fixed; buffer caps, optional-object defaults, OTA policy, and
board fallbacks are left to the executor.

### Seven Facets (audit trail)

1. Purpose: Make small/constrained devices first-class participants of the Sirena LwM2M platform with native embedded engineering (no JVM, no gateway indirection).
2. Objective: v1 ships an LwM2M 1.2 client (CoAP/UDP+DTLS and MQTT) that completes an end-to-end registration/telemetry/command flow on a constrained target.
3. Sequence: Omitted as a facet; only load-bearing order is codec/object layer before bindings/optional objects.
4. Rationale: Port natively instead of wrapping Zephyr’s 1.0/1.1 built-in client to preserve Sirena 1.2/MQTT wire parity.
5. Key decisions delegated: buffer sizing, optional-object Kconfig defaults, OTA policy, board-support fallbacks (Pi Zero 2 W verification).
6. Anti-goals: Do not sacrifice the smallest-device budget for feature breadth; do not build a server/gateway or teach the RTOS; do not starve app work with heavyweight management.
7. Constraints: Static pools/no heap in steady state; mandatory 0/1/3 only with others opt-in; Apache-2.0; distributed as a Zephyr module; Pi Zero 2 W support must be verified.

## The Story

Every ship needs a way to navigate the open sea. Every device needs a way to find
its place in the connected world.

Zephyr gives embedded devices the foundations to run: the kernel, the drivers,
the networking, and the hardware abstractions. But having a heartbeat is not the
same as having a voice.

Sirena brings that voice.

`@sirena-lwm2m/zephyr` is the bridge between the Zephyr RTOS and the Sirena
LwM2M ecosystem. It brings device management, resource modeling, registration,
observation, and remote operations into the constrained world of embedded
firmware.

No JVM. No unnecessary layers. Just native embedded engineering, designed to make
even the smallest devices first-class citizens of the connected world.

## Reference

* [The Zephyr Project. Zephyr](https://github.com/zephyrproject-rtos/zephyr) - a
new generation, scalable, optimized, secure RTOS for multiple hardware architectures
