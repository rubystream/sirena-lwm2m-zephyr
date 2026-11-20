# Agent Instructions

## Development & Code Style

- **Do not write comments in code.** The code should speak for itself.

## Repository Layout

- Implementation code lives under `src/`.
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
