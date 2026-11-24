# ESP32 DevKitC build and runtime verification

This guide walks a first-time user through building the Sirena LwM2M client for an
ESP32 board, flashing it, and checking that the device boots, connects, and talks
to an LwM2M server. Every command starts from the repository root. The example
board is the ESP32-DevKitC; other ESP32 boards use the same flow with their own
Zephyr board name.

## What you need

- An ESP32-DevKitC board (or another ESP32 board supported by Zephyr).
- A USB cable that can carry data, not only power.
- A WiFi network that the ESP32 can join, if you are testing networking.
- An LwM2M server reachable from that network, such as a Leshan server on your
  development machine.
- A computer that can run Docker and the project commands from
  [development.md](development.md).
- Optional for a serial console: a terminal program such as `picocom`, `minicom`,
  `screen`, or PuTTY. The DevKitC exposes its console through the on-board USB
  bridge, so no external adapter is required.

## 1. Check that the computer is ready

1. Open a terminal in the repository folder.
2. Check the Node.js version:

   ```sh
   npm run check
   ```

3. Check that Docker can run Zephyr:

   ```sh
   docker compose run --rm zephyr west --version
   ```

If either command fails, fix the host setup first by following
[development.md](development.md).

## 2. Prepare the Zephyr workspace once

Run these commands only the first time, or after resetting Docker volumes:

```sh
npm run init
npm run update
```

`npm run init` creates the external Zephyr workspace. `npm run update` downloads
the Zephyr source and modules.

## 3. Build the sample for the ESP32

From the repository root, run:

```sh
docker compose run --rm -T -w /opt/zephyr-workspace zephyr west build -b esp32_devkitc/esp32/procpu /workspace/src/samples/lwm2m_client -d /workspace/build/sample-esp32
```

What this does:

- Uses the `esp32_devkitc/esp32/procpu` board definition from Zephyr.
- Builds the sample application in `/workspace/src/samples/lwm2m_client`.
- Writes the board files under `build/sample-esp32`.

A successful build ends with a line like:

```text
Generating files from .../build/sample-esp32/zephyr/zephyr.elf for board: esp32_devkitc/esp32/procpu
```

and creates the application image:

```text
build/sample-esp32/zephyr/zephyr.bin
```

The image uses the simple boot path by default: it contains only the application
and no second-stage bootloader, and it provides no secure boot or OTA support.

To build only the smoke image instead, use:

```sh
docker compose run --rm -T -w /opt/zephyr-workspace zephyr west build -b esp32_devkitc/esp32/procpu /workspace/src/smoke -d /workspace/build/esp32-smoke
```

To build the sample with the entry configuration of your server, override the
Kconfig symbols on the command line. For example, to point the pre-shared key
sample at a server on your local network:

```sh
docker compose run --rm -T -w /opt/zephyr-workspace zephyr west build -b esp32_devkitc/esp32/procpu \
    /workspace/src/samples/lwm2m_client -d /workspace/build/sample-esp32 \
    --extra-conf /workspace/src/samples/lwm2m_client/prj-dtls.conf \
    -DCONFIG_SIRENA_LWM2M_SAMPLE_SERVER_URI='"coaps://192.0.2.10:5684"' \
    -DCONFIG_SIRENA_LWM2M_SAMPLE_SECURITY_MODE=0 \
    -DCONFIG_SIRENA_LWM2M_SAMPLE_IDENTITY='"sirena-sample"' \
    -DCONFIG_SIRENA_LWM2M_SAMPLE_SECRET_KEY='"0123456789abcdef"'
```

The `coaps` scheme selects the DTLS binding, so the build needs the DTLS binding
and its mbed TLS prerequisites, which `prj-dtls.conf` of the sample enables. The
default noSec configuration still needs the development gate
`CONFIG_SIRENA_LWM2M_TRANSPORT_NOSEC`, which the `prj.conf` of the sample
opens. The `prj.conf` of the sample is applied by `west` on its own, so it must
not be passed again.

## 4. Flash the ESP32

Hardware flashing is opt-in and runs in a privileged container with USB access.
Connect the board to the computer with the USB cable before running the
commands.

1. If you built into the default directory, use:

   ```sh
   npm run flash
   ```

   The npm script flashes from `/workspace/build`.

2. For the custom output directories used above, run:

   ```sh
   docker compose --profile flash run --rm -T -w /opt/zephyr-workspace flash west flash -d /workspace/build/sample-esp32
   ```

Notes:

- `west flash` writes the bootloader, the partition table and the application
  image for boards that need them.
- If the chip stays in download mode after flashing and does not boot the new
  image, flash again with a watchdog reset:

  ```sh
  docker compose --profile flash run --rm -T -w /opt/zephyr-workspace flash west flash -d /workspace/build/sample-esp32 --reset-type watchdog-reset
  ```

- Faster reflashes during development:

  ```sh
  docker compose --profile flash run --rm -T -w /opt/zephyr-workspace flash west flash -d /workspace/build/sample-esp32 --esp-skip-flashed
  ```

  or, to write only differing regions against a local cache:

  ```sh
  docker compose --profile flash run --rm -T -w /opt/zephyr-workspace flash west flash -d /workspace/build/sample-esp32 --esp-diff
  ```

If flashing fails:

- Confirm the board enumerated on the host with `lsusb`.
- Confirm the user can access `/dev/bus/usb`, or run the opt-in profile only
  after the device is connected.
- Try a different USB cable or USB port; charging-only cables do not carry data.
- Hold the BOOT button on the DevKitC while starting the flash if auto-reset does
  not enter download mode.

## 5. Boot the ESP32 and watch the console

1. Eject or close any serial monitor that holds the port.
2. Power the board from the USB supply or leave it connected after flashing.
3. Open a serial terminal at 115200 baud, 8 data bits, no parity, 1 stop bit,
   no flow control. The common device path is `/dev/ttyUSB0` or
   `/dev/ttyACM0`; on macOS use `/dev/cu.usbserial-*`.
4. Reset the board with the EN or RST button.

A successful boot starts with something like:

```text
*** Booting Zephyr OS build v4.x.x-... ***
```

If nothing appears:

- Confirm the cable carries data and the driver for the on-board bridge is
  installed.
- Try the other USB device path.
- Check that the terminal speed is 115200 baud.
- Re-flash with `--reset-type watchdog-reset`.
- On builds that use the USB Serial/JTAG controller, the chip can remain in
  download mode until it is power cycled.

For Espressif-specific convenience, the container toolchain also provides
`west espressif monitor`, but it needs access to the serial device; a host
terminal is usually simpler.

## 6. Runtime verification

The sample runs the Sirena client flow and prints status as it progresses. For a
network check:

1. Make sure the ESP32 can join your WiFi network. The LwM2M sample uses the
   Zephyr networking configuration of the build; join is part of the connection
   path exercised when the sample starts its transport.
2. Run an LwM2M server reachable from the ESP32 on the address configured in the
   build. The default sample server URI is `coap://127.0.0.1:5683`; override it
   at build time as shown in section 3, or change the Kconfig symbols in
   `src/samples/lwm2m_client/Kconfig.sample`.
3. Make sure the server IP and port are reachable through your router. A server
   running on `127.0.0.1` on the development machine is not reachable from the
   device unless that loopback address has been mapped to a host on the network.
4. Observe the serial log for successful registration, telemetry reports, and
   command handling.
5. If the board exposes an execute resource, trigger a reboot command from the
   server and confirm the sample records it and resets telemetry.

Record the following in the issue or PR:

- exact build command used;
- board revision and flash setup;
- serial boot log;
- network type and result;
- registration, telemetry, and command results;
- any config overlays or Kconfig changes needed.

## 7. Optional: repeatable ESP32 build command

For convenience, use these commands for repeated builds:

```sh
docker compose run --rm -T -w /opt/zephyr-workspace zephyr west build -b esp32_devkitc/esp32/procpu /workspace/src/samples/lwm2m_client -d /workspace/build/sample-esp32
```

Clean before a from-scratch build with:

```sh
docker compose run --rm -T zephyr rm -rf /workspace/build/sample-esp32
```

Flash the same artifact again with:

```sh
docker compose --profile flash run --rm -T -w /opt/zephyr-workspace flash west flash -d /workspace/build/sample-esp32
```

## References

- Project development guide: [development.md](development.md)
- Project sample guide: [sample.md](sample.md)
- Project conformance guide: [conformance.md](conformance.md)
- ESP32-DevKitC board documentation:
  https://docs.zephyrproject.org/latest/boards/espressif/esp32_devkitc/doc/index.html
- Espressif ESP32-DevKitC user guide:
  https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32/esp32-devkitc/index.html
- Zephyr getting started:
  https://docs.zephyrproject.org/latest/develop/getting_started/index.html
- Zephyr west build tool:
  https://docs.zephyrproject.org/latest/develop/west/build-flash-debug.html
- Zephyr ESP32 flashing and debugging:
  https://docs.zephyrproject.org/latest/boards/espressif/common/building-flashing.html
- Issue tracker for this repository:
  https://github.com/rubystream/sirena-lwm2m-zephyr/issues
