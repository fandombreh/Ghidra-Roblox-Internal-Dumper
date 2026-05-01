cmake_minimum_required(VERSION 3.15)
project(RobloxDumper)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option to build kernel driver (requires WDK)
option(BUILD_KERNEL_DRIVER "Build kernel driver module" OFF)

# Add subdirectories
add_subdirectory(Injection)
add_subdirectory(Dumping)

if(BUILD_KERNEL_DRIVER)
    add_subdirectory(Kernel)
endif()

# Set output directories to root for single-folder distribution
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Windows-specific settings
if(WIN32)
    # Disable some warnings
    add_compile_options(/W3)
endif()
