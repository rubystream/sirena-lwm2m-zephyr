# Development Guide

This guide expands the root `README.md` and is the detailed reference for the
containerized Zephyr development environment. Keep `README.md` concise and put
detailed setup, troubleshooting, and guardrails here.

## Host and Container Responsibilities

- The host runs only Node.js helpers (`npm run check`, orchestration scripts) and
  Docker Compose. It never runs `west`, CMake, compilers, or flash tools.
- All Zephyr tooling runs inside the Docker containers defined by
  `compose.yml` and `Dockerfile`.
- Source is bind-mounted from the repository root into `/workspace`, so edits are
  visible to the container immediately.
- External Zephyr workspace and compiler cache are stored in named Docker volumes
  and persist across container runs.

## Layout Rules

- Implementation code lives under `src/` (for example `src/smoke/`).
- Developer-facing documentation lives under `docs/`.
- The repository root keeps the project entry point, agent instructions, and the
  Zephyr/host build metadata expected by tooling: `README.md`, `AGENTS.md`,
  `compose.yml`, `Dockerfile`, `package.json`, `west.yml`, and npm scripts under
  `scripts/`.
- Generated output such as `build/`, `.west/`, `modules/`, and `twister-out/`
  is ignored by Git and must never be committed.

## First-Time Setup

Prerequisites:

- Git
- Node.js LTS `24.22.0` (see `.nvmrc`, `.node-version`, and `mise.toml`)
- Docker Engine with the Compose plugin

Clone and verify the host toolchain:

```sh
git clone https://github.com/rubystream/sirena-lwm2m-zephyr.git
cd sirena-lwm2m-zephyr
npm run check
```

Verify the container can run the pinned Zephyr toolchain:

```sh
docker compose run --rm zephyr west --version
```

Initialize and update the external Zephyr workspace:

```sh
npm run init
npm run update
```

Build and run the smoke target:

```sh
npm run build
npm run test
```

`npm run smoke` performs the initialization, update, and build in one command and
exits non-zero on any failure:

```sh
npm run smoke
```

A successful run prints `sirena zephyr smoke ok`.

## Daily Commands

| Task | Command |
| --- | --- |
| Verify Node.js version | `npm run check` |
| Initialize workspace (once or after volume reset) | `npm run init` |
| Update Zephyr checkout | `npm run update` |
| Build smoke target | `npm run build` |
| Run smoke target | `npm run test` |
| Build and run registry tests | `npm run test:registry` |
| Build and run codec tests | `npm run test:codec` |
| Build and run objects tests | `npm run test:objects` |
| Build and run objects tests with optional resources | `npm run test:objects-optional` |
| Remove build output | `npm run clean` |
| Full smoke workflow | `npm run smoke` |
| Flash firmware (opt-in) | `npm run flash` |
| Deploy firmware (opt-in) | `npm run deploy` |

Registry APIs and the built-in objects API are not thread-safe and must be
called from the engine thread.

Rebuild from scratch without removing dependencies:

```sh
npm run clean
npm run build
npm run test
```

Run arbitrary Zephyr tooling while remaining in Docker:

```sh
docker compose run --rm zephyr west --version
```

## Image Pinning

The Docker build pins the upstream Zephyr base image in `Dockerfile`:

```text
FROM zephyrprojectrtos/zephyr-build:v0.30.0
```

`compose.yml` builds a derived project image tagged
`sirena-lwm2m-zephyr-dev:v0.30.0` from that Dockerfile. The upstream base image
tag must remain `zephyrprojectrtos/zephyr-build:v0.30.0` unless a deliberate
upgrade is reviewed and documented. Verify it with:

```sh
grep '^FROM' Dockerfile
```

Rebuild the derived image after changes to `Dockerfile`:

```sh
docker compose build
```

## Caching

- `zephyr-workspace` volume mounted at `/opt/zephyr-workspace`: stores the
  initialized `west` manifest and checked-out Zephyr source. Reused across runs
  to avoid repeated downloads.
- `ccache` volume mounted at `/root/.ccache`: stores compiler cache output and
  speeds up rebuilds.
- Docker image layers cache the derived development image.
- `build/` holds the current build tree and is created under the bind-mounted
  repository path. It is ignored by Git.

Clear only the current build output:

```sh
npm run clean
```

Clear the compiler cache:

```sh
docker compose run --rm zephyr rm -rf /root/.ccache/*
```

Reset all named volumes, including the Zephyr workspace:

```sh
docker compose down -v
npm run init
npm run update
```

## Flashing and Deployment

Hardware flashing/deployment is opt-in and isolated behind the `flash` Compose
profile. It runs a privileged container with access to `/dev/bus/usb`.

Commands:

```sh
npm run flash
npm run deploy
```

Both run `west flash -d /workspace/build` inside the profiled container.

### Host Permissions

Flashing requires host access to USB programming/debugging hardware.

- User must have permission to access `/dev/bus/usb` (commonly via the
  `plugdev` group or udev rules provided by the probe vendor).
- Privileged containers bypass some checks but still depend on host kernel
  device nodes. Ensure the target device is connected before starting.
- On Linux, add the user to the relevant group or install vendor udev rules;
  re-login or reload udev after changes.

### Devices

- `/dev/bus/usb` is passed into the `flash` service.
- The connected programmer/debug probe (for example J-Link, ST-Link, or
  OpenOCD-supported adapter) must be detected by the host.
- Verify with `lsusb` before flashing.

### Safety Notes

- The `flash` profile is privileged and grants broad device access; use it
  only when flashing is required.
- Never run untrusted code in the privileged container.
- Confirm the correct target board and build artifact before flashing to avoid
  overwriting firmware on the wrong device.
- Keep the default `zephyr` service for everyday builds; do not enable the
  profile in CI unless hardware is provisioned.
- Disconnect or power-cycle devices carefully; avoid flashing during power
  instability.

## Troubleshooting

- `npm run check` fails on Node.js version: use Node.js `24.22.0` and reload the
  shell, or select it with `nvm use`, `mise`, or `asdf`.
- Docker permission errors: ensure the current user can access the Docker socket,
  then re-login.
- Image build failures: verify network access and rebuild with
  `docker compose build`.
- Corrupted or incomplete workspace: reset volumes with `docker compose down -v`,
  then run `npm run init` and `npm run update`.
- Stale object or cache errors: run `npm run clean`; if they persist, clear the
  `ccache` volume and rebuild.
- `west update` revisions may take time and network access.
- Flashing failures: connect the probe, run `lsusb`, verify host permissions, and
  use only `npm run flash` or `npm run deploy`.
- Run all commands from the repository root so Compose and npm resolve the
  correct paths.

## Reference

- [Container and npm scripts](../package.json)
- [Built-in LwM2M objects](objects.md)
- [Compose services](../compose.yml)
- [Docker build](../Dockerfile)
- [Project entry point](../README.md)
- [Agent instructions](../AGENTS.md)
- [Zephyr Project](https://github.com/zephyrproject-rtos/zephyr)
- [Zephyr flashing and debugging](https://docs.zephyrproject.org/latest/develop/flash_debug/index.html)
- [Docker Compose profiles](https://docs.docker.com/compose/profiles/)

## Codec Error Codes

Codec APIs use negative `errno` values: `-EINVAL` for invalid arguments, `-ENOMEM`
for insufficient capacity or configured limits, and `-ENOTSUP` for unsupported
formats or unimplemented decode paths.

## Objects Error Codes

The built-in objects module and the typed application API use the same negative
`errno` values: `-EINVAL` for a type mismatch or an invalid argument, `-ENOENT`
for an unknown object, instance or resource, `-ENOMEM` for a value that exceeds
the configured capacity, and `-ENOTSUP` for an operation the resource does not
support. See [Built-in LwM2M Objects](objects.md) for the full API reference, the
resource sets and the Kconfig symbols.
