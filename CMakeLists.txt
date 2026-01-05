# Copyright 2019 SwitchPy Team. All rights reserved.
# Licensed under the MIT license.
# Refer to the LICENSE file included.
#
# libnx CMake template for Nintendo Switch homebrew development.

# Replace this with the name of your application
set(HOMEBREW_APP "NX-ScreenUploader")

# Meta information about the app
set(APP_TITLE ${HOMEBREW_APP})
set(APP_AUTHOR "Sakari")

set(VERSION_MAJOR 0)
set(VERSION_MINOR 2)
set(VERSION_MICRO 0)
set(APP_VERSION
        "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_MICRO}")

cmake_minimum_required(VERSION 3.12)
project(${HOMEBREW_APP})

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(options)
include(utils)
include(FetchContent)

find_package(LIBNX REQUIRED)
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)
if (NOT LIBNX_FOUND)
    cmake_panic("Unable to detect libnx on this system.")
endif ()
if (NOT CURL_FOUND)
    cmake_panic("Unable to detect libcurl on this system.")
endif ()
if (NOT ZLIB_FOUND)
    cmake_panic("Unable to detect zlib on this system.")
endif ()

include_directories(${PROJECT_BINARY_DIR})
include_directories(${PORTLIBS}/include)
include_directories(${LIBNX}/include)

if (NOT DEFINED CMAKE_BUILD_TYPE_INIT)
    set(CMAKE_BUILD_TYPE_INIT Release)
endif ()

set(CMAKE_C_STANDARD 99)
set(CXX_STANDARD_REQUIRED)
set(CMAKE_CXX_STANDARD 23)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    include(clang-utils)
endif ()

# Apply LTO settings if enabled
if (ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)
    if (ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
        cmake_info("LTO (Link Time Optimization) enabled")
    else ()
        cmake_warn("LTO is not supported by the compiler: ${ipo_output}")
    endif ()
else ()
    cmake_info("LTO (Link Time Optimization) disabled")
endif ()

function(fetch_minini)
    FetchContent_Declare(minIni
        GIT_REPOSITORY https://github.com/ITotalJustice/minIni-nx.git
        GIT_TAG 6e952b6
    )

    set(NINTENDO_SWITCH ON)

    set(MININI_USE_NX ${NINTENDO_SWITCH})
    set(MININI_USE_STDIO NOT ${NINTENDO_SWITCH})
    set(MININI_USE_FLOAT OFF)

    FetchContent_MakeAvailable(minIni)
    
    # Get the source directory and add it to include paths
    FetchContent_GetProperties(minIni SOURCE_DIR MININI_SOURCE_DIR)
    include_directories(${MININI_SOURCE_DIR}/include)
endfunction(fetch_minini)

fetch_minini()

include(nx-utils)

cmake_info("Building ${APP_TITLE} version ${APP_VERSION}.")

include(src/CMakeLists.txt)

target_link_libraries(${HOMEBREW_APP}.elf ${CURL_LIBRARIES} ${ZLIB_LIBRARIES} switch::libnx minIni)
set_target_properties(${HOMEBREW_APP}.elf PROPERTIES
        LINKER_LANGUAGE CXX # Replace this with C if you have C source files
        LINK_FLAGS "-specs=${LIBNX}/switch.specs -march=armv8-a+simd+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -Wl,--as-needed -Wl,--gc-sections -Wl,--strip-all -Wl,-Map,.map")

#build_switch_binaries(test.elf)
#build_switch_sysmodule(${HOMEBREW_APP}.elf)
build_switch_nsp(${HOMEBREW_APP}.elf)
