// File: src/utils/cuda_helper.cpp
#include "utils/cuda_helper.h"
#include <cuda_runtime.h>

#include <cstring>
#include <iostream>
#include <sstream>

namespace medical::imaging {

CudaHelper& CudaHelper::getInstance() {
    static CudaHelper instance;
    return instance;
}

CudaHelper::CudaHelper() :
    cudaAvailable_(false),
    deviceCount_(0),
    currentDevice_(0),
    lastError_(cudaSuccess) {

    initialize();
}

CudaHelper::~CudaHelper() = default;

bool CudaHelper::initialize() {
    lastError_ = cudaGetDeviceCount(&deviceCount_);

    if (lastError_ != cudaSuccess) {
        lastErrorString_ = cudaGetErrorString(lastError_);
        std::cerr << "CUDA initialization failed: " << lastErrorString_ << std::endl;
        cudaAvailable_ = false;
        deviceCount_ = 0;
        return false;
    }

    cudaAvailable_ = (deviceCount_ > 0);

    if (cudaAvailable_) {
        lastError_ = cudaGetDevice(&currentDevice_);
        if (lastError_ != cudaSuccess) {
            lastErrorString_ = cudaGetErrorString(lastError_);
            std::cerr << "Failed to get current CUDA device: " << lastErrorString_ << std::endl;
        }

        cudaDeviceProp props{};
        lastError_ = cudaGetDeviceProperties(&props, currentDevice_);
        if (lastError_ == cudaSuccess) {
            std::cout << "Using CUDA device: " << props.name
                      << " (Compute capability: " << props.major << "." << props.minor << ")"
                      << std::endl;
        }
    } else {
        std::cout << "No CUDA devices found" << std::endl;
    }

    return cudaAvailable_;
}

bool CudaHelper::isCudaAvailable() const {
    return cudaAvailable_;
}

int CudaHelper::getDeviceCount() const {
    return deviceCount_;
}

bool CudaHelper::setDevice(int deviceId) {
    if (!cudaAvailable_ || deviceId >= deviceCount_) {
        return false;
    }

    lastError_ = cudaSetDevice(deviceId);
    if (lastError_ == cudaSuccess) {
        currentDevice_ = deviceId;
        return true;
    }

    lastErrorString_ = cudaGetErrorString(lastError_);
    std::cerr << "Failed to set CUDA device: " << lastErrorString_ << std::endl;
    return false;
}

int CudaHelper::getCurrentDevice() const {
    return currentDevice_;
}

cudaDeviceProp CudaHelper::getDeviceProperties(int deviceId) const {
    cudaDeviceProp props{};
    memset(&props, 0, sizeof(cudaDeviceProp));

    if (!cudaAvailable_) {
        return props;
    }

    if (deviceId < 0) {
        deviceId = currentDevice_;
    }

    if (deviceId >= deviceCount_) {
        return props;
    }

    cudaGetDeviceProperties(&props, deviceId);
    return props;
}

cudaError_t CudaHelper::allocateMemory(void** devPtr, const size_t size) {
    if (!cudaAvailable_) {
        return cudaErrorInitializationError;
    }

    lastError_ = cudaMalloc(devPtr, size);
    if (lastError_ != cudaSuccess) {
        lastErrorString_ = cudaGetErrorString(lastError_);
        std::cerr << "CUDA memory allocation failed: " << lastErrorString_ << std::endl;
    }

    return lastError_;
}

cudaError_t CudaHelper::freeMemory(void* devPtr) {
    if (!cudaAvailable_ || !devPtr) {
        return cudaErrorInvalidValue;
    }

    lastError_ = cudaFree(devPtr);
    if (lastError_ != cudaSuccess) {
        lastErrorString_ = cudaGetErrorString(lastError_);
        std::cerr << "CUDA memory free failed: " << lastErrorString_ << std::endl;
    }

    return lastError_;
}

cudaError_t CudaHelper::copyHostToDevice(void* devPtr, const void* hostPtr, size_t size) {
    if (!cudaAvailable_ || !devPtr || !hostPtr) {
        return cudaErrorInvalidValue;
    }

    lastError_ = cudaMemcpy(devPtr, hostPtr, size, cudaMemcpyHostToDevice);
    if (lastError_ != cudaSuccess) {
        lastErrorString_ = cudaGetErrorString(lastError_);
        std::cerr << "Host to device copy failed: " << lastErrorString_ << std::endl;
    }

    return lastError_;
}

cudaError_t CudaHelper::copyDeviceToHost(void* hostPtr, const void* devPtr, size_t size) {
    if (!cudaAvailable_ || !devPtr || !hostPtr) {
        return cudaErrorInvalidValue;
    }

    lastError_ = cudaMemcpy(hostPtr, devPtr, size, cudaMemcpyDeviceToHost);
    if (lastError_ != cudaSuccess) {
        lastErrorString_ = cudaGetErrorString(lastError_);
        std::cerr << "Device to host copy failed: " << lastErrorString_ << std::endl;
    }

    return lastError_;
}

cudaError_t CudaHelper::registerHostMemory(void* hostPtr, size_t size) {
    if (!cudaAvailable_ || !hostPtr) {
        return cudaErrorInvalidValue;
    }

    lastError_ = cudaHostRegister(hostPtr, size, cudaHostRegisterDefault);
    if (lastError_ != cudaSuccess) {
        lastErrorString_ = cudaGetErrorString(lastError_);
        std::cerr << "Host memory registration failed: " << lastErrorString_ << std::endl;
    }

    return lastError_;
}

cudaError_t CudaHelper::unregisterHostMemory(void* hostPtr) {
    if (!cudaAvailable_ || !hostPtr) {
        return cudaErrorInvalidValue;
    }

    lastError_ = cudaHostUnregister(hostPtr);
    if (lastError_ != cudaSuccess) {
        lastErrorString_ = cudaGetErrorString(lastError_);
        std::cerr << "Host memory unregistration failed: " << lastErrorString_ << std::endl;
    }

    return lastError_;
}

void* CudaHelper::mapGpuMemoryToCpu(void* devPtr, size_t size) {
    if (!cudaAvailable_ || !devPtr) {
        return nullptr;
    }

    void* mappedPtr = nullptr;

    // Use cudaHostRegister for this pointer to make it accessible by CPU
    lastError_ = cudaHostRegister(devPtr, size, cudaHostRegisterMapped);
    if (lastError_ != cudaSuccess) {
        lastErrorString_ = cudaGetErrorString(lastError_);
        std::cerr << "GPU memory mapping failed: " << lastErrorString_ << std::endl;
        return nullptr;
    }

    // Get the mapped pointer
    lastError_ = cudaHostGetDevicePointer(&mappedPtr, devPtr, 0);
    if (lastError_ != cudaSuccess) {
        lastErrorString_ = cudaGetErrorString(lastError_);
        std::cerr << "Getting mapped pointer failed: " << lastErrorString_ << std::endl;
        cudaHostUnregister(devPtr);
        return nullptr;
    }

    return mappedPtr;
}

void CudaHelper::unmapGpuMemory(void* mappedPtr) const {
    if (!cudaAvailable_ || !mappedPtr) {
        return;
    }

    cudaHostUnregister(mappedPtr);
}

cudaError_t CudaHelper::registerExternalMemory(void* extPtr, size_t size,
                                      unsigned int flags, void** cudaPtr) {
    if (!cudaAvailable_ || !extPtr || !cudaPtr) {
        return cudaErrorInvalidValue;
    }

    // Register the external memory with CUDA
    lastError_ = cudaHostRegister(extPtr, size, flags);
    if (lastError_ != cudaSuccess) {
        lastErrorString_ = cudaGetErrorString(lastError_);
        std::cerr << "External memory registration failed: " << lastErrorString_ << std::endl;
        return lastError_;
    }

    // Get a device pointer that can be used for GPU operations
    lastError_ = cudaHostGetDevicePointer(cudaPtr, extPtr, 0);
    if (lastError_ != cudaSuccess) {
        lastErrorString_ = cudaGetErrorString(lastError_);
        std::cerr << "Getting device pointer failed: " << lastErrorString_ << std::endl;
        cudaHostUnregister(extPtr);
    }

    return lastError_;
}

bool CudaHelper::supportsUnifiedMemory(int deviceId) const {
    if (!cudaAvailable_) {
        return false;
    }

    if (deviceId < 0) {
        deviceId = currentDevice_;
    }

    cudaDeviceProp props = getDeviceProperties(deviceId);

    // Unified memory requires compute capability 3.0 or higher
    return (props.major > 3 || (props.major == 3 && props.minor >= 0));
}

cudaError_t CudaHelper::allocateUnifiedMemory(void** ptr, size_t size) {
    if (!cudaAvailable_) {
        return cudaErrorInitializationError;
    }

    if (!supportsUnifiedMemory()) {
        return cudaErrorNotSupported;
    }

    lastError_ = cudaMallocManaged(ptr, size, cudaMemAttachGlobal);
    if (lastError_ != cudaSuccess) {
        lastErrorString_ = cudaGetErrorString(lastError_);
        std::cerr << "Unified memory allocation failed: " << lastErrorString_ << std::endl;
    }

    return lastError_;
}

std::string CudaHelper::getLastErrorString() const {
    return lastErrorString_;
}

cudaError_t CudaHelper::synchronize() {
    if (!cudaAvailable_) {
        return cudaErrorInitializationError;
    }

    lastError_ = cudaDeviceSynchronize();
    if (lastError_ != cudaSuccess) {
        lastErrorString_ = cudaGetErrorString(lastError_);
        std::cerr << "Device synchronization failed: " << lastErrorString_ << std::endl;
    }

    return lastError_;
}

std::string CudaHelper::getCudaVersionInfo() const {
    std::stringstream ss;

    if (!cudaAvailable_) {
        ss << "CUDA not available";
        return ss.str();
    }

    int runtimeVersion = 0;
    int driverVersion = 0;

    cudaRuntimeGetVersion(&runtimeVersion);
    cudaDriverGetVersion(&driverVersion);

    ss << "CUDA Runtime: " << runtimeVersion / 1000 << "."
       << (runtimeVersion % 100) / 10 << ", Driver: "
       << driverVersion / 1000 << "." << (driverVersion % 100) / 10;

    return ss.str();
}

} // namespace medical::imaging