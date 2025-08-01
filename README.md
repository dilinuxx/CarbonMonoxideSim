CarbonMonoxideSim - Carbon Monoxide Sensor Simulation Using Blockchain
======================================================================

This project simulates carbon monoxide (CO) monitoring using datasets and stores sensor data on a lightweight blockchain implemented in C++. It supports deployment across macOS, Linux, Android, and iOS platforms and includes smart contract logic for hazard response simulation.

--------------------------------------------------------------------------------
Folder Structure Assumed
--------------------------------------------------------------------------------

```
CarbonMonoxideSim/
├── CMakeLists.txt                 # Cross-platform build configuration
├── src/                           # All source code files
│   ├── main.cpp                   # Entry point
│   ├── Blockchain.cpp
│   ├── Block.cpp
│   ├── Storage.cpp
│   ├── dataset_loader.cpp         # Loads .csv/.xlsx datasets as SensorEvents
├── include/                       # All headers
│   ├── Blockchain.hpp
│   ├── Block.hpp
│   ├── Storage.hpp
│   ├── SmartContract.hpp
│   ├── SensorEvent.hpp
├── datasets/
│   ├── dataset_1.csv              # Gas sensor array dataset
│   ├── dataset_2.csv              # Air Quality dataset (converted from .xlsx)
│   ├── dataset_3.csv              # Gas Turbine Emissions dataset
└── build/                         # Output build directory (generated by CMake)
```

--------------------------------------------------------------------------------
To Build on Each Platform
--------------------------------------------------------------------------------

macOS (with Homebrew installed)
-----------------------------------
Install dependencies:
```
    brew install openssl sqlite3 cmake
```

Then build the project:
```
    cmake -Bbuild -H.
    cmake --build build
```

Linux (Amazon Linux / Ubuntu)
-----------------------------------
Install dependencies:
```
    sudo yum install openssl-devel sqlite-devel cmake      # Amazon Linux
```
    # OR for Ubuntu/Debian:
```
    sudo apt install libssl-dev libsqlite3-dev cmake
```

Then build the project:
```
    cmake -Bbuild -H.
    cmake --build build
```

Android (via Android NDK Toolchain)
-----------------------------------
Set your environment:
```
    export ANDROID_NDK=/path/to/android-ndk
```

Then build with:
```
    cmake -Bbuild -H. \
      -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a \
      -DANDROID_NATIVE_API_LEVEL=21
    cmake --build build
```

iOS (via ios-cmake)
----------------------
Clone toolchain:
```
    git clone https://github.com/leetal/ios-cmake.git
```

Then build with:
```
    cmake -Bbuild -H. \
      -DCMAKE_TOOLCHAIN_FILE=ios-cmake/ios.toolchain.cmake \
      -DIOS_PLATFORM=OS64 \
      -DENABLE_BITCODE=0
    cmake --build build
```

--------------------------------------------------------------------------------
Features
--------------------------------------------------------------------------------
- Lightweight blockchain storing simulated sensor events
- Supports multiple real-world datasets as simulation inputs
- SmartContract system for triggering alarms or hazard notifications
- Modular and cross-platform C++17 design
- JSON-based data encoding for blockchain compatibility
- SQLite3 as a local lightweight persistent store
- OpenSSL SHA256 used for hashing blocks

--------------------------------------------------------------------------------
Datasets Used
--------------------------------------------------------------------------------
- Dataset 1: Gas sensor array (CO, Temperature, RH, 14 resistances)
- Dataset 2: Air Quality dataset (CO(GT), T, RH, AH, etc.)
- Dataset 3: Gas Turbine Emission dataset (TIT, CO, NOX, etc.)

All datasets should be placed in the `datasets/` directory and formatted as `.csv`.

--------------------------------------------------------------------------------
Suggested Next Steps
--------------------------------------------------------------------------------
- Integrate with machine learning model for CO prediction
- Add real-time sensor stream (from hardware or simulated sources)
- Improve blockchain security (encryption, digital signatures)
- Deploy to mobile device and use push notifications for alerts

--------------------------------------------------------------------------------
License
--------------------------------------------------------------------------------
MIT License – You are free to use, modify, and distribute this software with attribution.

--------------------------------------------------------------------------------
Credits
--------------------------------------------------------------------------------
Developed as part of a research project focused on carbon monoxide hazard detection,
indoor air quality monitoring, and data provenance for safety-critical applications.
