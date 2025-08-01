cmake_minimum_required(VERSION 3.15)

project(CarbonMonoxideSim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ========== Platform-Specific Configuration ==========
if(APPLE)
    message(STATUS "Building on macOS or iOS")

    # Use Homebrew paths for macOS
    if(NOT DEFINED ENV{IOS_PLATFORM})
        set(HOMEBREW_PREFIX "/opt/homebrew")
        include_directories(${HOMEBREW_PREFIX}/include)
        link_directories(${HOMEBREW_PREFIX}/lib)
    endif()

elseif(ANDROID)
    message(STATUS "Building for Android")
    # Assume Android toolchain is configured externally (e.g. with android-cmake)
elseif(UNIX)
    message(STATUS "Building on Linux")
endif()

# ========== Find Dependencies ==========
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# For SQLite, fallback to pkg-config if find_package(SQLite3) fails
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SQLITE3 REQUIRED sqlite3)
    include_directories(${SQLITE3_INCLUDE_DIRS})
    link_directories(${SQLITE3_LIBRARY_DIRS})
endif()

# ========== Source Files ==========
file(GLOB_RECURSE SOURCES
    src/*.cpp
    include/*.hpp
)

# ========== Executable ==========
add_executable(CarbonMonoxideSim ${SOURCES})

# ========== Include Directories ==========
target_include_directories(CarbonMonoxideSim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OPENSSL_INCLUDE_DIR}
    ${SQLite3_INCLUDE_DIRS}
)

# ========== Linked Libraries ==========
target_link_libraries(CarbonMonoxideSim PRIVATE
    Threads::Threads
    OpenSSL::Crypto
)

# SQLite linking (depends on how it's found)
if(SQLite3_FOUND)
    target_link_libraries(CarbonMonoxideSim PRIVATE SQLite::SQLite3)
else()
    target_link_libraries(CarbonMonoxideSim PRIVATE ${SQLITE3_LIBRARIES})
endif()

# ========== Warnings and Flags ==========
target_compile_options(CarbonMonoxideSim PRIVATE -Wall -Wextra -Wpedantic)

# ========== iOS and Android Notes ==========
# You must set CMAKE_TOOLCHAIN_FILE for iOS/Android builds manually:
# Example iOS:
#   cmake -DCMAKE_TOOLCHAIN_FILE=ios-cmake/ios.toolchain.cmake -DIOS_PLATFORM=OS64 ..
#
# Example Android:
#   cmake -DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK}/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a ..