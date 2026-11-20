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

## Repository Layout

- `src/` - implementation code for the Sirena LwM2M Zephyr client.
- `docs/` - developer-facing documentation.
- Repository root - project entry point (`README.md`), agent instructions
  (`AGENTS.md`), and Zephyr module build metadata expected by `west`
  (`CMakeLists.txt`, `zephyr/module.yml` when introduced by later epics).
  Root metadata is intentionally retained at the root and not moved.
- Generated build, cache, and output paths are ignored by Git (see `.gitignore`).


## First-time Setup

1. Verify the host toolchain: `npm run check`.
2. Build and run the smoke target: `npm run smoke`.

See `docs/development.md` for the full first-time setup, caching, and
troubleshooting.

## Daily Commands

Day-to-day work uses the npm scripts documented in `docs/development.md`
(`build`, `test`, `clean`, `smoke`, and the opt-in `flash` / `deploy`).

## Host Toolchain

This repository uses Node.js LTS `24.22.0` as its host toolchain. The version is
pinned through project metadata (`.nvmrc`, `.node-version`, `mise.toml`) and
enforced by `package.json` `engines.node` (`>=24.0.0 <25.0.0`).

Contributors should use a Node.js LTS `24.x` installation. Version managers such
as `nvm`, `mise`, or `asdf` pick up the pinned version automatically from the
metadata files.

Verify the host Node.js version:

```sh
npm run check
```

Host scripts only orchestrate repository workflows. They never compile, flash, or
install the Zephyr SDK.

## Containerized Zephyr Dev Environment

The Zephyr build environment is provided as a container based on the pinned
base image `zephyrprojectrtos/zephyr-build:v0.30.0`. The image requires no host
devices by default.

Bring up the dev container:

```sh
docker compose run --rm zephyr west --version
```

Source is mounted from the repository root into `/workspace`; a named
`zephyr-workspace` volume holds the external Zephyr workspace, and a named
`ccache` volume persists the compiler cache across runs. The default service runs without
`privileged`, devices, or added capabilities; hardware flashing is out of scope
for the default container.

Flashing/deployment is opt-in via the `flash` Compose profile, which runs a
privileged container with `/dev/bus/usb` access. See `docs/development.md` for
host permissions, device access, and safety notes.

## Build and Smoke Workflow

Container commands are exposed through npm scripts:

```sh
npm run init     # west init -l /workspace /opt/zephyr-workspace
npm run update   # west update
npm run build    # west build -b native_sim /workspace/src/smoke
npm run test     # west build -t run
npm run clean    # rm -rf /workspace/build
npm run flash    # west flash (opt-in profile; requires host USB device permissions)
npm run deploy   # same as npm run flash (opt-in profile)
npm run smoke    # init, update, and build the smoke target
```

`npm run smoke` performs a headless initialization, update, and build of the
minimal `src/smoke` target, which prints `sirena zephyr smoke ok`. The chain
exits non-zero on any failure so CI can report success or failure.

## Reference

* [The Zephyr Project. Zephyr](https://github.com/zephyrproject-rtos/zephyr) - a
new generation, scalable, optimized, secure RTOS for multiple hardware architectures
