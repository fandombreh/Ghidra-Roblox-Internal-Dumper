cmake_minimum_required(VERSION 3.15)
project(RobloxDumperKernel)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# WDK Configuration
set(CMAKE_SYSTEM_VERSION 10.0)
set(NTDDI_VERSION 0x0A000006)  # Windows 10

set(KERNEL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(KERNEL_SOURCES
    driver_main.cpp
)

# Try to find WDK
find_path(WDK_INCLUDE_DIR ntddk.h
    PATHS
    "C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/km"
    "C:/Program Files (x86)/Windows Kits/10/Include/10.0.26200.0/km"
    "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/km"
    "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22000.0/km"
    "C:/Program Files (x86)/Windows Kits/10/Include/10.0.19041.0/km"
    "C:/Windows Kits/10/Include/*/km"
)

# Also try to find any WDK installation
if(NOT WDK_INCLUDE_DIR)
    file(GLOB WDK_VERSIONS "C:/Program Files (x86)/Windows Kits/10/Include/*/km")
    if(WDK_VERSIONS)
        list(GET WDK_VERSIONS 0 WDK_INCLUDE_DIR)
    endif()
endif()

if(NOT WDK_INCLUDE_DIR)
    message(WARNING "WDK headers not found. Kernel driver will not build.")
    message(WARNING "Install WDK and set WDK_INCLUDE_DIR manually if needed.")
    return()
endif()

message(STATUS "Found WDK headers at: ${WDK_INCLUDE_DIR}")

add_library(RobloxDumperKernel SHARED ${KERNEL_SOURCES})

set_target_properties(RobloxDumperKernel PROPERTIES
    SUFFIX ".sys"
    POSITION_INDEPENDENT_CODE OFF
)

target_compile_definitions(RobloxDumperKernel PRIVATE
    _X86_
    _AMD64_
    NTDDI_VERSION=0x0A000006
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    _KERNEL_MODE
    _NTDDK_
    NTSTRSAFE_V2=1
)

target_include_directories(RobloxDumperKernel PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${WDK_INCLUDE_DIR}
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_compile_definitions(RobloxDumperKernel PRIVATE _AMD64_)
    target_compile_options(RobloxDumperKernel PRIVATE /machine:x64)
else()
    target_compile_definitions(RobloxDumperKernel PRIVATE _X86_)
    target_compile_options(RobloxDumperKernel PRIVATE /machine:x86)
endif()
