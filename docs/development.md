# Development Guide

## Containerized Zephyr Environment

The default `zephyr` Compose service is device-free. It builds and tests the
smoke target in `src/smoke` without host hardware access.

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

## Reference

- [Zephyr Project: Flashing and Debugging](https://docs.zephyrproject.org/latest/develop/flash_debug/index.html)
- [Docker Compose profiles](https://docs.docker.com/compose/profiles/)
- [The Zephyr Project](https://github.com/zephyrproject-rtos/zephyr)
