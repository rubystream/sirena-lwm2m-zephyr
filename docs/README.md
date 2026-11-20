# docs

Developer-facing documentation for the Sirena LwM2M Zephyr module lives here.

Narrative, setup, build, and contribution guides belong under `docs/`. The
root `README.md` remains the project entry point and keeps the high-level
intent; detailed documentation lives here to avoid bloating the root.

- `development.md` - containerized development environment, daily commands, and
  troubleshooting.
- `objects.md` - built-in Security, Server and Device objects, the typed
  application API, provisioning, and the Kconfig symbols.
- `transport.md` - the transport interface, the engine thread and the
  CoAP/UDP binding, with the registration lifecycle and the error codes.

Generated documentation output and build artifacts are ignored and never
committed.
