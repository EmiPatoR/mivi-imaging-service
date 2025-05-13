#pragma once

#include <cuda_runtime.h>
#include <string>

namespace medical::imaging {

/**
 * @class CudaHelper
 * @brief Manages CUDA operations and provides GPU memory management utilities
 */
class CudaHelper {
public:
    // Get singleton instance
    static CudaHelper& getInstance();

    // Initialize CUDA
    bool initialize();

    // Check if CUDA is available
    [[nodiscard]] bool isCudaAvailable() const;

    // Get number of available CUDA devices
    [[nodiscard]] int getDeviceCount() const;

    // Set active device
    bool setDevice(int deviceId);

    // Get current device
    [[nodiscard]] int getCurrentDevice() const;

    // Get device properties
    [[nodiscard]] cudaDeviceProp getDeviceProperties(int deviceId = -1) const;

    // Allocate GPU memory
    cudaError_t allocateMemory(void** devPtr, size_t size);

    // Free GPU memory
    cudaError_t freeMemory(void* devPtr);

    // Copy memory from host to device
    cudaError_t copyHostToDevice(void* devPtr, const void* hostPtr, size_t size);

    // Copy memory from device to host
    cudaError_t copyDeviceToHost(void* hostPtr, const void* devPtr, size_t size);

    // Register host memory for zero-copy (pinned memory)
    cudaError_t registerHostMemory(void* hostPtr, size_t size);

    // Unregister host memory
    cudaError_t unregisterHostMemory(void* hostPtr);

    // Get a pointer that can be used by CPU to access GPU memory
    void* mapGpuMemoryToCpu(void* devPtr, size_t size);

    // Unmap GPU memory
    void unmapGpuMemory(void* mappedPtr) const;

    // Register external memory (like DMA buffer) with CUDA
    cudaError_t registerExternalMemory(void* extPtr, size_t size,
                               unsigned int flags, void** cudaPtr);

    // Check if device supports unified memory
    [[nodiscard]] bool supportsUnifiedMemory(int deviceId = -1) const;

    // Allocate unified memory (accessible from both CPU and GPU)
    cudaError_t allocateUnifiedMemory(void** ptr, size_t size);

    // Check for CUDA errors and get error string
    [[nodiscard]] std::string getLastErrorString() const;

    // Synchronize device
    cudaError_t synchronize();

    // Get CUDA version info
    [[nodiscard]] std::string getCudaVersionInfo() const;


    // Prevent copy
    CudaHelper(const CudaHelper&) = delete;
    CudaHelper& operator=(const CudaHelper&) = delete;

private:
    CudaHelper();
    ~CudaHelper();

    bool cudaAvailable_;
    int deviceCount_;
    int currentDevice_;
    cudaError_t lastError_;
    std::string lastErrorString_;

};

} // namespace medical::imaging