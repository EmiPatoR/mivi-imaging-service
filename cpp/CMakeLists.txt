cmake_minimum_required(VERSION 3.14)
project(MedicalUltrasoundImaging VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(Protobuf_PROTOC_EXECUTABLE /usr/local/bin/protoc)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)


# Find required packages
find_package(Protobuf REQUIRED CONFIG PATHS /usr/local/lib/cmake/protobuf NO_DEFAULT_PATH)
find_package(gRPC REQUIRED)
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


# Proto files
set(PROTO_FILES ${PROTO_DIR}/imaging_service.proto)

# Create target for Protobuf
add_library(ultrasound_proto INTERFACE)
protobuf_generate(
        TARGET ultrasound_proto
        LANGUAGE cpp
        IMPORT_DIRS ${PROTO_DIR}
        PROTOS ${PROTO_FILES}
)

# Locate grpc_cpp_plugin explicitly
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

message(STATUS "gRPC plugin found at: ${GRPC_CPP_PLUGIN}")


# Create target for gRPC
add_library(ultrasound_grpc INTERFACE)
protobuf_generate(
        TARGET ultrasound_grpc
        LANGUAGE grpc
        GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
        IMPORT_DIRS ${PROTO_DIR}
        PROTOS ${PROTO_FILES}
        PLUGIN "protoc-gen-grpc=${GRPC_CPP_PLUGIN}"
)

# Core imaging library
add_library(ultrasound_imaging SHARED
        ${SRC_DIR}/device/device_manager.cpp
        ${SRC_DIR}/device/ultrasound_device.cpp
        ${SRC_DIR}/device/blackmagic_device.cpp
        ${SRC_DIR}/frame/frame.cpp
        ${SRC_DIR}/frame/frame_processor.cpp
        ${SRC_DIR}/frame/segmentation.cpp
        ${SRC_DIR}/communication/shared_memory.cpp
        ${SRC_DIR}/communication/grpc_server.cpp
        ${SRC_DIR}/api/imaging_service.cpp
        ${SRC_DIR}/api/device_discovery.cpp
        ${SRC_DIR}/api/calibration.cpp
        ${SRC_DIR}/api/ffi_bindings.cpp
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
        protobuf::libprotobuf
        gRPC::grpc++
        ultrasound_proto
        ultrasound_grpc
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

install(FILES ${PROTO_HDRS} ${GRPC_HDRS}
        DESTINATION include/generated
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