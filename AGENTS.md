# Agent Instructions

## Development & Code Style

- **Do not write comments in code.** The code should speak for itself.

## Tooling and Placement Guardrails

- Zephyr tooling must run only in Docker via `docker compose` or the npm scripts. Never install or invoke it on the host.
- Implementation code lives under `src/`; developer-facing documentation lives under `docs/`.

## Repository Layout

- Implementation code lives under `src/`. Place source, headers, and
  implementation assets there; do not put implementation files at the repository
  root.
- Developer-facing documentation lives under `docs/`.
- Zephyr module build metadata (`CMakeLists.txt`, `zephyr/module.yml`) is
  retained at the repository root as expected by `west`.
- The root `README.md` is the project entry point; detailed docs go in `docs/`.
- Generated build/cache/output paths are ignored by Git.

## Issue tracker: GitHub

Issues and specs for this repo live as GitHub issues. Use the `gh` CLI for all operations.

## Git & GitHub Workflow

### Absolute rule: never commit to `main`

- **`main` is protected. Never commit, force-push, or push directly to it.**
- **All changes must land through a pull request (PR).** No exceptions — not even for docs, typos, or urgent fixes.
- Never commit secrets, `.env` files, credentials, or generated build output.
- Start every change on a dedicated branch created off an up-to-date `main`:

  ```bash
  git switch main
  git pull --ff-only
  git switch -c <type>/<short-description>
  ```

### Branch naming

Use a lowercase, slash-separated prefix:

| Prefix      | Use for                                   |
| ----------- | ----------------------------------------- |
| `feat/`     | New feature                               |
| `fix/`      | Bug fix                                   |
| `docs/`     | Documentation only                        |
| `refactor/` | Code change that neither fixes nor adds   |
| `chore/`    | Tooling, deps, config                     |
| `test/`     | Tests only                                |
| `epic/`     | Epic branch                               |
| `issue/`    | Issue branch                               |

Example: `feat/post-tag-filter`, `fix/sqlite-content-sync`.

## Containerized Zephyr Build Environment

- Zephyr tooling (`west`, configure, build, test, and flash, including all
  toolchain/compiler/debug-tools) runs only inside Docker. The host must not
  compile Zephyr, flash devices, install the SDK, or run `west` directly.
- Base image is pinned to `zephyrprojectrtos/zephyr-build:v0.30.0`.
- The default Compose service runs without privileged access, devices, or added
  capabilities; hardware access is not granted by default.
- Use `docker compose run --rm zephyr <command>` to run Zephyr toolchain commands.

Flashing/deployment requires the opt-in `flash` Compose profile (privileged, USB device access). Default container stays device-free.

## Container Build Commands

npm scripts wrap the containerized Zephyr workflow:

- `npm run init` - `docker compose run --rm -T zephyr sh -c 'test -d /opt/zephyr-workspace/.west || west init -l /workspace /opt/zephyr-workspace'`
- `npm run update` - `docker compose run --rm -T -w /opt/zephyr-workspace zephyr west update`
- `npm run build` - `docker compose run --rm -T -w /opt/zephyr-workspace zephyr west build -b native_sim /workspace/src/smoke -d /workspace/build`
- `npm run test` - `docker compose run --rm -T -w /opt/zephyr-workspace zephyr west build -t run -d /workspace/build`
- `npm run clean` - `docker compose run --rm -T zephyr rm -rf /workspace/build`
- `npm run smoke` - init, update, then build
- `npm run build:sample` - build the native_sim sample
- `npm run flash` - `docker compose --profile flash run --rm flash west flash -d /workspace/build` (opt-in profile; requires host USB device permissions)
- `npm run deploy` - same as `npm run flash` (opt-in profile; requires host USB device permissions)

The minimal smoke target lives under `src/smoke/` and prints
`sirena zephyr smoke ok`.

## Host Toolchain

- Node.js LTS `24.22.0` is the pinned host toolchain, declared via `.nvmrc`,
  `.node-version`, `mise.toml`, and `package.json` `engines.node`.
- Verify the active Node.js version with `npm run check` before running host
  workflows.
- Host scripts orchestrate only; they must not compile Zephyr, flash devices,
  or install the SDK.
