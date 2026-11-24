# Raspberry Pi Pico 2 W build and runtime verification

This guide walks a first-time user through building the Sirena LwM2M client for a
Raspberry Pi Pico 2 W, flashing it, and checking that the board boots, joins WiFi,
and talks to an LwM2M server. Every command starts from the repository root.

## What you need

- A Raspberry Pi Pico 2 W board.
- A USB cable that carries data.
- A 3.3 V USB-to-UART adapter and three jumper wires for the serial console.
- A WiFi network that the Pico can join.
- An LwM2M server reachable from that network, such as a Leshan or Sirena server.
- A computer that can run Docker and the project commands from
  [development.md](development.md).
- The WiFi firmware blobs for the Infineon CYW43439 module.

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

`npm run init` creates the external Zephyr workspace. `npm run update` downloads the
Zephyr source and modules. After `npm run update`, fetch the WiFi firmware blobs
required by the Pico 2 W:

```sh
docker compose run --rm -T -w /opt/zephyr-workspace zephyr west blobs fetch hal_infineon
```

This downloads the Infineon firmware and CLM data for the CYW43439. Without it,
WiFi builds fail with CMake errors about missing firmware files.

## 3. Create a temporary WiFi configuration

The sample has no WiFi setup of its own. Create a temporary file named
`pico2w-wifi.conf` in the repository root. It is temporary and contains
credentials, so remove it after validation.

```ini
CONFIG_WIFI=y
CONFIG_WIFI_NM=y
CONFIG_WIFI_NM_WPA_SUPPLICANT=y
CONFIG_NET_L2_WIFI_MGMT=y

CONFIG_WIFI_CREDENTIALS=y
CONFIG_WIFI_CREDENTIALS_STATIC=y
CONFIG_WIFI_CREDENTIALS_STATIC_SSID="YOUR_SSID"
CONFIG_WIFI_CREDENTIALS_STATIC_PASSWORD="YOUR_PASSWORD"
CONFIG_WIFI_CREDENTIALS_STATIC_TYPE_PSK=y
CONFIG_WIFI_CREDENTIALS_CONNECT_STORED=y

CONFIG_NETWORKING=y
CONFIG_NET_DRIVERS=y
CONFIG_NET_UDP=y
CONFIG_NET_IPV4=y
CONFIG_NET_SOCKETS=y
CONFIG_NET_DHCPV4=y
```

Replace `YOUR_SSID` and `YOUR_PASSWORD` with the test network values. The default
security type above is WPA2-PSK.

## 4. Build the sample for the Pico 2 W

From the repository root, override the server URI and run time in the command.
The board target is `rpi_pico2/rp2350a/m33_0/w`.

```sh
docker compose run --rm -T -w /opt/zephyr-workspace zephyr west build \
    -b rpi_pico2/rp2350a/m33_0/w \
    /workspace/src/samples/lwm2m_client \
    -d /workspace/build/sample-pico2w \
    --extra-conf /workspace/pico2w-wifi.conf \
    -DCONFIG_SIRENA_LWM2M_SAMPLE_SERVER_URI='"coap://<server-lan-ip>:5683"' \
    -DCONFIG_SIRENA_LWM2M_SAMPLE_RUN_SECONDS=60
```

What this does:

- Uses the Pico 2 W board definition, including the CYW43439 WiFi interface.
- Builds the Sirena sample in `/workspace/src/samples/lwm2m_client`.
- Applies the temporary WiFi overlay and the default sample `prj.conf`.
- Sets the server address to a host on the network and gives the run a 60-second
  lifetime.

The sample's `prj.conf` is applied automatically by west, so do not pass it
again. A successful build creates, among other files:

```text
build/sample-pico2w/zephyr/zephyr.uf2
```

To build only a smoke image for a quick first boot check, use:

```sh
docker compose run --rm -T -w /opt/zephyr-workspace zephyr west build \
    -b rpi_pico2/rp2350a/m33_0/w \
    /workspace/src/smoke \
    -d /workspace/build/pico2w-smoke
```

## 5. Flash the Pico 2 W

The Pico exposes a UF2 bootloader through the `BOOTSEL` button.

1. Unplug the board.
2. Hold `BOOTSEL` and connect the USB cable.
3. Release `BOOTSEL`; the board appears as a USB mass storage device.
4. Copy `build/sample-pico2w/zephyr/zephyr.uf2` to that device.
5. The board resets and runs the new image.

Additional runners supported by the board include `openocd`, `picotool`, `pyocd`,
`probe-rs`, and `jlink`; use them only if SWD debugging hardware is available.

## 6. Connect the serial console

The default console is UART0 at 115200 baud, 8 data bits, no parity, 1 stop bit,
no flow control. Wire the 3.3 V USB-to-UART adapter before powering the board:

- Adapter GND to Pico GND.
- Adapter RX to Pico GP0 (UART0 TX).
- Adapter TX to Pico GP1 (UART0 RX).

Open a serial terminal at 115200 baud on the adapter device path, then power the
board with USB.

A successful boot starts with something like:

```text
*** Booting Zephyr OS build v4.x.x-... ***
```

If nothing appears:

- Confirm the adapter is 3.3 V.
- Swap RX and TX once.
- Check that the cable carries data.
- Confirm the mass-storage flash completed and the board reset.

## 7. Runtime verification

The sample runs the full Sirena flow and prints its progress.

1. Make sure the LwM2M server is reachable from the WiFi network at the address
   configured in section 4. A server on `127.0.0.1` on the development machine is
   not reachable from the board unless that address has been mapped onto the
   network.
2. Watch the serial log for WiFi association, an IP address, registration,
   telemetry reports, and notifications.
3. From the server, trigger the execute resource on Device object `3/0/4` and
   confirm the log records the command and resets telemetry.
4. Confirm the sample reaches its 60-second limit, deregisters, and exits.

## 8. Optional DTLS follow-up

After the noSec path is known good, rebuild with the DTLS overlay and PSK
settings:

```sh
docker compose run --rm -T -w /opt/zephyr-workspace zephyr west build \
    -b rpi_pico2/rp2350a/m33_0/w \
    /workspace/src/samples/lwm2m_client \
    -d /workspace/build/sample-pico2w-dtls \
    --extra-conf /workspace/pico2w-wifi.conf \
    --extra-conf /workspace/src/samples/lwm2m_client/prj-dtls.conf \
    -DCONFIG_SIRENA_LWM2M_SAMPLE_SERVER_URI='"coaps://<server-lan-ip>:5684"' \
    -DCONFIG_SIRENA_LWM2M_SAMPLE_SECURITY_MODE=0 \
    -DCONFIG_SIRENA_LWM2M_SAMPLE_IDENTITY='"sirena-sample"' \
    -DCONFIG_SIRENA_LWM2M_SAMPLE_SECRET_KEY='"0123456789abcdef"' \
    -DCONFIG_SIRENA_LWM2M_SAMPLE_RUN_SECONDS=60
```

Flash `build/sample-pico2w-dtls/zephyr/zephyr.uf2` and repeat the runtime
verification.

## 9. Record the result

Record the following in the issue or PR:

- exact build command used;
- board revision and cable/adapter setup;
- serial boot log;
- WiFi association and IP result;
- registration, telemetry, notification, and command results;
- any config overlays or Kconfig changes needed;
- whether the noSec path and, if attempted, the DTLS path passed.

Clean a build directory before a from-scratch attempt:

```sh
docker compose run --rm -T zephyr rm -rf /workspace/build/sample-pico2w
```

Remove `pico2w-wifi.conf` after validation. It is temporary and contains WiFi
credentials.

## References

- Project development guide: [development.md](development.md)
- Project sample guide: [sample.md](sample.md)
- Project conformance guide: [conformance.md](conformance.md)
- Zephyr Raspberry Pi Pico 2 documentation:
  https://docs.zephyrproject.org/latest/boards/raspberrypi/rpi_pico2/doc/index.html
- Raspberry Pi Pico getting started:
  https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf
- Issue tracker for this repository:
  https://github.com/rubystream/sirena-lwm2m-zephyr/issues
EOF