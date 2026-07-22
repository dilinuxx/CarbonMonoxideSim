------------------------------
## ESP32-H2 OpenThread CLI Bring-Up Guide
This repository contains the documentation and steps required to bring up an ESP32-H2 development board using the ESP-IDF framework and the OpenThread CLI example.
The ESP32-H2 features an integrated IEEE 802.15.4 radio, making it a low-power, cost-effective node for building Thread mesh networks without relying on resident Wi-Fi infrastructure.
------------------------------
## Hardware Requirements

* ESP32-H2 Development Board (e.g., ESP32-H2-DevKitM-1, Olimex ESPH2-DevKit-LiPo)
* USB-C Data Cable
* Host Machine (Linux, macOS, or Windows with ESP-IDF installed)

------------------------------
## Environment Setup
Ensure your ESP-IDF environment is properly installed and sourced. This guide targets ESP-IDF v5.2 or later.

# Source the ESP-IDF environment tools
. $HOME/esp/esp-idf/export.sh
# Verify your installation version
idf.py --version

Expected Output: ESP-IDF v5.2.x (or newer)
------------------------------
## Building the OpenThread CLI Application
We use the native ot_cli example provided inside the ESP-IDF directory tree.

# Navigate to the OpenThread CLI example directory
cd $IDF_PATH/examples/openthread/ot_cli
# Set the build target explicitly to the ESP32-H2 chip architectural type
idf.py set-target esp32h2
# Compile the firmware binaries
idf.py build

Once compilation finishes successfully, you will see a Project build complete confirmation message on your terminal.
------------------------------
## Flashing & Monitoring
Connect your ESP32-H2 board to your computer using the USB port.
## 1. Identify the Serial Port
Identify your connected device port mapping name:

* macOS: ls /dev/cu.usbmodem* or ls /dev/cu.usbserial*
* Linux: ls /dev/ttyACM* or ls /dev/ttyUSB*

## 2. Flash Firmware and Launch Monitor
Replace /dev/cu.usbmodem1101 with your actual device port string:

idf.py -p /dev/cu.usbmodem1101 flash monitor

------------------------------
## Operating the OpenThread CLI
Once the device boots up and the monitor establishes a connection, press Enter to see the OpenThread command-line prompt (>).
## Verify Firmware Status

> version
OPENTHREAD/1.3.0; esp32h2; Jul 22 2026 
Done

> state
disabled
Done

## Initialise and Start a New Thread Mesh Network
Run the following configuration sequence to commit a network dataset operational profile and start the stack:

# 1. Generate a new randomized operational dataset configuration 
> dataset init new
Done
# 2. Commit this dataset instance as active configuration memory
> dataset commit active
Done
# 3. Bring up the network interface layer
> ifconfig up
Done
# 4. Enable the local Thread stack operations
> thread start
Done

## Verify Network State
Wait 3–5 seconds for network negotiations to finish, then poll the node state status:

> state
leader
Done

The node will automatically upgrade its structural mesh state assignment to leader, indicating that your standalone micro-mesh network backbone structure is operational.