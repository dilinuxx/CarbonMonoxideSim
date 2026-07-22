# ESP32-H2 OpenThread CLI Bring-Up Guide

This repository documents the process of building, flashing, and running the native OpenThread CLI example on an **ESP32-H2** development board using **ESP-IDF v5.2**.

The ESP32-H2 features an integrated IEEE 802.15.4 radio, making it an ideal platform for developing **Thread mesh networking** applications.

---

## Features

- Build the native OpenThread CLI example
- Flash firmware to an ESP32-H2
- Create a standalone Thread mesh network
- Verify Thread Leader operation
- Troubleshooting common ESP-IDF and serial connection issues

---

## Hardware Requirements

- ESP32-H2 Development Board
- USB Data Cable
- Computer running macOS, Linux, or Windows
- ESP-IDF v5.2 or later

---

## Software Requirements

- ESP-IDF v5.2+
- Python 3.x
- Git

Verify your ESP-IDF installation:

```bash
idf.py --version
```

Expected output:

```
ESP-IDF v5.2.x
```

---

# Environment Setup

Source the ESP-IDF environment before using `idf.py`.

```bash
cd ~/esp
source esp-idf/export.sh
```

---

# Build the OpenThread CLI Example

Navigate to the OpenThread example:

```bash
cd ~/esp/esp-idf/examples/openthread/ot_cli
```

Select the ESP32-H2 target:

```bash
idf.py set-target esp32h2
```

Build the project:

```bash
idf.py build
```

When complete you should see:

```
Project build complete.
```

---

# Flash the Firmware

Identify your serial device.

### macOS

```bash
ls /dev/cu.usbserial*
```

or

```bash
ls /dev/cu.usbmodem*
```

### Linux

```bash
ls /dev/ttyUSB*
```

or

```bash
ls /dev/ttyACM*
```

Flash the firmware:

```bash
idf.py -p <serial-port> flash
```

Example:

```bash
idf.py -p /dev/cu.usbserial-110 flash
```

---

# Open the Serial Monitor

```bash
idf.py -p <serial-port> monitor
```

Example:

```bash
idf.py -p /dev/cu.usbserial-110 monitor
```

After booting, the OpenThread CLI prompt appears:

```
>
```

---

# Verify the Firmware

Check the firmware version:

```
> version
```

Check the Thread state:

```
> state
```

Expected:

```
disabled
Done
```

---

# Create a New Thread Network

Generate a new operational dataset:

```
> dataset init new
```

Commit the dataset:

```
> dataset commit active
```

Bring up the network interface:

```
> ifconfig up
```

Start Thread:

```
> thread start
```

Wait a few seconds and verify the node state:

```
> state
```

Expected output:

```
leader
Done
```

The ESP32-H2 is now acting as the **Leader** of a new Thread mesh network.

---

# View Network Information

Network Name

```
> networkname
```

PAN ID

```
> panid
```

Extended PAN ID

```
> extpanid
```

IPv6 Addresses

```
> ipaddr
```

---

# Troubleshooting

## CMakeLists.txt not found

If you see:

```
CMakeLists.txt not found in project directory
```

Ensure you are inside the OpenThread example directory:

```bash
cd ~/esp/esp-idf/examples/openthread/ot_cli
```

---

## Serial Port Busy

If the serial port is busy:

```
Resource busy
```

Determine which process owns the port:

```bash
lsof /dev/cu.usbserial-110
```

Terminate the process:

```bash
kill <PID>
```

Restart the monitor:

```bash
idf.py -p /dev/cu.usbserial-110 monitor
```

---

## Device Not Configured

If the monitor disconnects with:

```
Device not configured
```

This is typically caused by:

- USB cable disconnection
- Board reset
- USB-UART reset
- Temporary power interruption

Reconnect the device and restart the monitor.

---

## Flash Size Warning

If you see:

```
Detected size(4096k) larger than the size in the binary image header(2048k)
```

Your development board contains a 4 MB flash chip while the firmware was built for a 2 MB flash layout. The example still runs correctly, but production builds should be configured for the correct flash size.

---

# Expected Result

A successful bring-up should produce:

```
> state
leader

> networkname
OpenThread

> panid
0x9b5c

> ipaddr
fdde:ad00:beef:...
```

Your ESP32-H2 is now operating as the **Leader** of a Thread mesh network and is ready for additional Thread devices to join.

---

# License

This project is released under the MIT License.