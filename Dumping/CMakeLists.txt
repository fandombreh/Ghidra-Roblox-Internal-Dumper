cmake_minimum_required(VERSION 3.15)
project(RobloxDumper)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DUMPER_SOURCES
    dumper.cpp
    kernel_client.cpp
)

add_executable(Dumper ${DUMPER_SOURCES})

# Set output to parent directory (without Debug subfolder)
set_target_properties(Dumper PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}
)

if(WIN32)
    target_link_libraries(Dumper PRIVATE psapi)
endif()
