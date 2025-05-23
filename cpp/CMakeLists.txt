cmake_minimum_required(VERSION 3.14)
project(MedicalUltrasoundImaging VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(Protobuf_PROTOC_EXECUTABLE /usr/local/bin/protoc)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Find required packages
find_package(Threads REQUIRED)

# Option to build shared library (for FFI)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# Directory structure
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(BLACKMAGIC_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ext/blackmagic_sdk)
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests)

# Include directories
include_directories(
        ${INCLUDE_DIR}
        ${BLACKMAGIC_SDK_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# Blackmagic SDK library
add_library(blackmagic_sdk STATIC
        ${BLACKMAGIC_SDK_DIR}/DeckLinkAPIDispatch.cpp
)

# Configure compilation flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(blackmagic_sdk PRIVATE -Wall -Wextra -fpermissive -fPIC)
endif()


# Core imaging library
add_library(ultrasound_imaging SHARED
        ${SRC_DIR}/device/device_manager.cpp
        ${SRC_DIR}/device/blackmagic_device.cpp
        ${SRC_DIR}/frame/frame.cpp
        ${SRC_DIR}/communication/shared_memory.cpp
        ${SRC_DIR}/api/imaging_service.cpp
)

# Include generated directories
target_include_directories(ultrasound_imaging
        PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}
        ${INCLUDE_DIR}
)

# Link protobuf and gRPC libraries and generated files
target_link_libraries(ultrasound_imaging
        PUBLIC
        blackmagic_sdk
        rt  # For POSIX shared memory
        pthread # For threading
        Threads::Threads
        dl
)

# Executables
add_executable(imaging_service_daemon ${SRC_DIR}/main.cpp)
target_link_libraries(imaging_service_daemon ultrasound_imaging)

# Export include directories for dependent targets
target_include_directories(ultrasound_imaging
        PUBLIC
        $<BUILD_INTERFACE:${INCLUDE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
        $<INSTALL_INTERFACE:include>
)

# Install rules
install(TARGETS ultrasound_imaging blackmagic_sdk imaging_service_daemon
        EXPORT UltrasoundImagingTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include
)

install(DIRECTORY ${INCLUDE_DIR}/
        DESTINATION include
        FILES_MATCHING PATTERN "*.h"
)

# Tests
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(${TEST_DIR})
    endif()
endif()

# CPack configuration
include(CPack)