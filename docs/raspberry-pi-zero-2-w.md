# Raspberry Pi Zero 2 W build and runtime verification

This guide walks a first-time user through building the Sirena LwM2M client for the
Raspberry Pi Zero 2 W, installing it on an SD card, and checking that the device
boots, connects, and talks to an LwM2M server. Every command starts from the
repository root.

## What you need

- A Raspberry Pi Zero 2 W board.
- A microSD card (8 GB or larger is plenty).
- An SD card reader connected to your computer.
- A 3.3 V USB-to-UART adapter and three jumper wires.
- A USB power supply for the Pi.
- A WiFi network that the Pi can join, if you are testing networking.
- A computer that can run Docker and the project commands from
  [development.md](development.md).

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

## 3. Build the sample for the Raspberry Pi Zero 2 W

From the repository root, run:

```sh
docker compose run --rm -T -w /opt/zephyr-workspace zephyr west build -b rpi_zero_2w /workspace/src/samples/lwm2m_client -d /workspace/build/sample-pi
```

What this does:

- Uses the `rpi_zero_2w` board definition from Zephyr.
- Builds the sample application in `/workspace/src/samples/lwm2m_client`.
- Writes the board files under `build/sample-pi`.

A successful build ends with a line like:

```text
Generating files from .../build/sample-pi/zephyr/zephyr.elf for board: rpi_zero_2w/bcm2710
```

The file you will copy later is:

```text
build/sample-pi/zephyr/zephyr.bin
```

To build only the smoke image instead, use:

```sh
docker compose run --rm -T -w /opt/zephyr-workspace zephyr west build -b rpi_zero_2w /workspace/src/smoke -d /workspace/build/pi-smoke
```

## 4. Prepare the SD card

The Pi Zero 2 W boots from a microSD card. Zephyr does not replace the VideoCore
firmware; it only replaces the kernel file that the firmware loads.

1. Flash a Raspberry Pi OS Lite (64-bit) image to the microSD card using Raspberry
   Pi Imager. This creates the FAT boot partition with the required firmware
   blobs: `bootcode.bin`, `fixup.dat`, and `start.elf`.
2. Insert the SD card into your computer after flashing.
3. Open the FAT boot partition in your file manager.

## 5. Copy Zephyr onto the SD card

In the boot partition:

1. Copy the file `build/sample-pi/zephyr/zephyr.bin` from your repository to the
   root of the boot partition.
2. Replace the existing `config.txt` file with exactly this content:

   ```text
   arm_64bit=1
   enable_uart=1
   core_freq=250
   kernel_address=0x200000
   kernel=zephyr.bin
   ```

Notes:

- `arm_64bit=1` is required when the kernel filename does not start with
  `kernel8`.
- `enable_uart=1` sends the console to GPIO 14/15. Without this, the Bluetooth
  radio claims those pins.
- `core_freq=250` keeps the mini-UART baud rate stable.
- `kernel_address=0x200000` must match the DRAM base in the Zephyr devicetree.

## 6. Boot the Pi and watch the console

1. Eject the SD card safely from your computer.
2. Insert it into the Raspberry Pi Zero 2 W.
3. Wire the USB-to-UART adapter:
   - Adapter GND to Pi pin 6 (GND).
   - Adapter RX to Pi pin 8 (GPIO 14, UART TX).
   - Adapter TX to Pi pin 10 (GPIO 15, UART RX).
4. Plug the adapter into your computer and open a serial terminal at 115200 baud,
   8 data bits, no parity, 1 stop bit, no flow control. The exact program does not
   matter; any serial monitor works.
5. Power the Pi with the USB power supply.
6. Watch the serial terminal for boot output.

A successful boot starts with something like:

```text
*** Booting Zephyr OS build v4.x.x-... ***
```

If nothing appears:

- Check that the adapter is 3.3 V.
- Swap RX and TX wires once.
- Confirm `enable_uart=1` is in `config.txt`.
- Confirm the SD card boot partition is FAT and contains the firmware files.

## 7. Runtime verification

The sample runs the Sirena client flow and prints status as it progresses. For a
network check:

1. Make sure the Pi can join your WiFi network. The current `rpi_zero_2w` board
   support in Zephyr does not include onboard WiFi, so use a supported network
   path for the test build or document the limitation observed on your hardware.
2. Run an LwM2M server reachable from the Pi on the address configured in the
   sample. The default sample server URI is `coap://127.0.0.1:5683`; override it
   at build time by passing a configuration file or changing the Kconfig symbols
   in `src/samples/lwm2m_client/Kconfig.sample`.
3. Observe the serial log for successful registration, telemetry reports, and
   command handling.
4. If the board exposes an execute resource, trigger a reboot command from the
   server and confirm the sample records it and resets telemetry.

Record the following in the issue or PR:

- exact build command used;
- board revision and SD card setup;
- serial boot log;
- network type and result;
- registration, telemetry, and command results;
- any config overlays or Kconfig changes needed.

## 8. Optional: repeatable Pi build command

For convenience, add a local shell alias or note this command for repeated
builds:

```sh
docker compose run --rm -T -w /opt/zephyr-workspace zephyr west build -b rpi_zero_2w /workspace/src/samples/lwm2m_client -d /workspace/build/sample-pi
```

Clean before a from-scratch build with:

```sh
docker compose run --rm -T zephyr rm -rf /workspace/build/sample-pi
```

## References

- Project development guide: [development.md](development.md)
- Project sample guide: [sample.md](sample.md)
- Project conformance guide: [conformance.md](conformance.md)
- Raspberry Pi Zero 2 W board documentation:
  https://docs.zephyrproject.org/latest/boards/raspberrypi/rpi_zero_2w/doc/index.html
- Raspberry Pi config.txt reference:
  https://www.raspberrypi.com/documentation/computers/config_txt.html
- Raspberry Pi OS and Imager:
  https://www.raspberrypi.com/software/
- Zephyr getting started:
  https://docs.zephyrproject.org/latest/develop/getting_started/index.html
- Zephyr west build tool:
  https://docs.zephyrproject.org/latest/develop/west/build-flash-debug.html
- Issue tracker for this repository:
  https://github.com/rubystream/sirena-lwm2m-zephyr/issues
