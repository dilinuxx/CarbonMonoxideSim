Sure. I'll make the README document the exact setup we've established: **ESP32-H2-DevKit-N4**, ESP-IDF 5.2, OpenThread CLI, native USB JTAG for flashing, and the separate UART bridge for the Thread CLI/monitor.

# ESP32-H2 OpenThread CLI — DevKit-N4 Setup

This project uses the **ESP32-H2-DevKit-N4** with ESP-IDF 5.2 and the ESP-IDF OpenThread CLI example.

The ESP32-H2 provides IEEE 802.15.4 hardware for Thread and supports OpenThread as a native Thread device.

## Hardware

**Board:** ESP32-H2-DevKit-N4

The board exposes two USB serial interfaces when connected to macOS:

| Interface                 | macOS device                  | Purpose                         |
| ------------------------- | ----------------------------- | ------------------------------- |
| Espressif USB JTAG/Serial | `/dev/cu.usbmodem1401`        | Flashing and USB JTAG/debug     |
| USB UART / Single Serial  | `/dev/cu.usbmodem59710821721` | OpenThread CLI / serial monitor |

The important distinction is that the OpenThread CLI is configured to use **UART**, while the ESP-IDF flashing/debug interface uses the **native USB JTAG/Serial interface**.

## Software

The project was created from the ESP-IDF OpenThread CLI example:

```text
~/esp/esp-idf/examples/openthread/ot_cli
```

A separate working project was created as:

```text
~/esp/esp32h2_thread
```

ESP-IDF version:

```text
ESP-IDF v5.2
```

Verify ESP-IDF:

```bash
idf.py --version
```

Verify that ESP32-H2 is supported:

```bash
idf.py --list-targets
```

The output should include:

```text
esp32h2
```

## Create the project

Copy the ESP-IDF OpenThread CLI example:

```bash
cd ~/esp
cp -R esp-idf/examples/openthread/ot_cli esp32h2_thread
cd esp32h2_thread
```

Set the target to ESP32-H2:

```bash
idf.py set-target esp32h2
```

This recreates the project configuration for the ESP32-H2.

## Build

Build the OpenThread CLI application:

```bash
idf.py build
```

A successful build produces:

```text
build/bootloader/bootloader.bin
build/partition_table/partition-table.bin
build/esp_ot_cli.bin
```

The application used in this setup is approximately 1 MB and fits within the configured 2 MB flash partition.

## OpenThread configuration

The project is configured with:

```text
CONFIG_IDF_TARGET="esp32h2"
CONFIG_IDF_TARGET_ESP32H2=y

CONFIG_IEEE802154_ENABLED=y

CONFIG_OPENTHREAD_ENABLED=y
CONFIG_OPENTHREAD_RADIO_NATIVE=y
CONFIG_OPENTHREAD_FTD=y
CONFIG_OPENTHREAD_CLI=y
```

The device is configured as an OpenThread **FTD (Full Thread Device)**.

The CLI is enabled so that Thread can be configured interactively from the serial terminal.

The default network configuration includes:

```text
Network name: OpenThread-ESP
PAN ID:       0x1234
Channel:      15
```

The OpenThread CLI itself is configured for UART:

```text
CONFIG_OPENTHREAD_CONSOLE_TYPE_UART=y
```

This is why the UART serial interface is used for the Thread CLI.

## Flashing the ESP32-H2

Use the native Espressif USB JTAG/Serial interface for flashing:

```bash
idf.py -p /dev/cu.usbmodem1401 flash
```

This interface is identified by macOS as:

```text
USB JTAG/serial debug unit
Manufacturer: Espressif
Vendor ID: 0x303a
Product ID: 0x1001
```

Do not confuse this with the USB UART interface.

## Starting the OpenThread CLI

After flashing, connect to the UART interface:

```bash
idf.py -p /dev/cu.usbmodem59710821721 monitor
```

The OpenThread CLI should eventually display a prompt:

```text
>
```

The UART interface is identified by macOS as the USB Single Serial interface.

Example:

```text
USB Single Serial
Vendor ID: 0x1a86
Product ID: 0x55d3
```

## Verify OpenThread

At the `>` prompt, check the OpenThread state:

```text
state
```

Initially the result may be:

```text
disabled
Done
```

This is normal. It means the OpenThread stack has been initialized but Thread has not yet been started.

Check the OpenThread version:

```text
version
```

Check the current channel:

```text
channel
```

For example:

```text
> state
disabled
Done

> version
openthread-esp32/...
Done

> channel
11
Done
```

The channel reported by the running OpenThread instance can differ from the compile-time default in `sdkconfig`, because the active Thread dataset determines the operational network configuration.

## Inspect the active Thread dataset

Before starting Thread, inspect the current dataset:

```text
dataset active -x
```

Also inspect the human-readable dataset:

```text
dataset
```

Check the interface state:

```text
ifconfig
```

Do not start Thread until the dataset and configuration have been checked.

## Start Thread

Once the dataset has been configured, the normal sequence is:

```text
ifconfig up
thread start
```

Then check:

```text
state
```

A successfully formed Thread network should eventually report a role such as:

```text
leader
```

or:

```text
router
```

depending on the state of the Thread network.

## Important: two serial ports

For this ESP32-H2-DevKit-N4 setup, remember:

### Flash / USB JTAG

```text
/dev/cu.usbmodem1401
```

Use:

```bash
idf.py -p /dev/cu.usbmodem1401 flash
```

### OpenThread UART CLI

```text
/dev/cu.usbmodem59710821721
```

Use:

```bash
idf.py -p /dev/cu.usbmodem59710821721 monitor
```

The two interfaces have different purposes.

## Troubleshooting

### `idf.py: command not found`

Load the ESP-IDF environment:

```bash
source ~/esp/esp-idf/export.sh
```

Then verify:

```bash
idf.py --version
```

### Verify the board is connected

```bash
ls /dev/cu.*
```

For this setup, the two relevant devices are normally:

```text
/dev/cu.usbmodem1401
/dev/cu.usbmodem59710821721
```

Device names can change after reconnecting the board or changing USB connections. If necessary, inspect the USB devices with:

```bash
system_profiler SPUSBDataType
```

### Verify the ESP32-H2

The native USB interface can be tested with:

```bash
esptool.py --port /dev/cu.usbmodem1401 chip_id
```

Expected output includes:

```text
Detecting chip type... ESP32-H2
Chip is ESP32-H2
Features: BLE, IEEE802.15.4
USB mode: USB-Serial/JTAG
```

### CLI does not appear

Make sure the monitor is connected to the **UART interface**, not the native USB JTAG interface:

```bash
idf.py -p /dev/cu.usbmodem59710821721 monitor
```

The OpenThread configuration contains:

```text
CONFIG_OPENTHREAD_CONSOLE_TYPE_UART=y
```

Therefore the Thread CLI is expected on UART.

## Current workflow

The complete development workflow is:

```bash
cd ~/esp/esp32h2_thread
```

Build:

```bash
idf.py build
```

Flash using USB JTAG:

```bash
idf.py -p /dev/cu.usbmodem1401 flash
```

Open the OpenThread CLI using UART:

```bash
idf.py -p /dev/cu.usbmodem59710821721 monitor
```

Then:

```text
state
version
channel
dataset active -x
dataset
ifconfig
```

From there, configure or create the Thread network and start Thread:

```text
ifconfig up
thread start
state
```

This setup deliberately keeps **firmware flashing/debugging** and the **OpenThread CLI UART console** on their respective interfaces.
