#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <map>

#include "frame/frame.h"

namespace medical::imaging {
    /**
     * @enum DeviceFeature
     * @brief Features that may be supported by the device
     */
    enum class DeviceFeature {
        DIRECT_MEMORY_ACCESS,     // Direct memory access (DMA)
        GPU_DIRECT,               // Direct to GPU memory
        HARDWARE_TIMESTAMP,       // Hardware-based timestamping
        EXTERNAL_SYNC,            // External synchronization
        FRAME_METADATA,           // Rich frame metadata
        MULTIPLE_STREAMS,         // Multiple simultaneous streams
        PROGRAMMABLE_ROI,         // Programmable region of interest
        HARDWARE_COMPRESSION      // Hardware-based compression
    };

    /**
     * @class UltrasoundDevice
     * @brief Enhanced abstract base class for ultrasound devices with zero-copy support
     */
    class UltrasoundDevice {
    public:
        /**
         * @brief Status codes for device operations
         */
        enum class Status {
            OK,                     // Operation completed successfully
            DEVICE_NOT_FOUND,       // Device not found or not connected
            INIT_FAILED,            // Failed to initialize device
            ALREADY_STREAMING,      // Device is already streaming
            NOT_STREAMING,          // Device is not currently streaming
            CONFIGURATION_ERROR,    // Invalid configuration
            FEATURE_NOT_SUPPORTED,  // Requested feature not supported
            PERMISSION_DENIED,      // Permission denied (e.g., access to device)
            TIMEOUT,                // Operation timed out
            IO_ERROR,               // I/O error
            INTERNAL_ERROR          // Unspecified internal error
        };

        /**
         * @brief Configuration for device initialization with zero-copy options
         */
        struct Config {
            int width;                      // Desired capture width
            int height;                     // Desired capture height
            double frameRate;               // Desired frame rate
            std::string pixelFormat;        // Desired pixel format
            bool enableAudio;               // Whether to capture audio as well
            bool enableDirectMemoryAccess;  // Enable DMA if supported
            bool enableGpuDirect;           // Enable direct GPU transfer if supported
            BufferType preferredBufferType; // Preferred buffer type
            std::string sharedMemoryName;   // Shared memory region for direct output
            size_t bufferCount;             // Number of buffers to allocate
            bool enableHardwareTimestamps;  // Use hardware timestamps if available

            // External memory allocation callback
            std::function<void*(size_t size)> externalAllocCallback;

            // External memory free callback
            std::function<void(void*)> externalFreeCallback;

            // Constructor with default values
            Config() :
                width(1920),
                height(1080),
                frameRate(60.0),
                pixelFormat("YUV"),
                enableAudio(false),
                enableDirectMemoryAccess(false),
                enableGpuDirect(false),
                preferredBufferType(BufferType::CPU_MEMORY),
                sharedMemoryName(""),
                bufferCount(3),
                enableHardwareTimestamps(false),
                externalAllocCallback(nullptr),
                externalFreeCallback(nullptr) {}
        };

        /**
         * @brief Device capabilities
         */
        struct Capabilities {
            bool supportsDma;                   // Device supports DMA
            bool supportsGpuDirect;             // Device supports direct-to-GPU transfer
            bool supportsHardwareTimestamps;    // Device supports hardware timestamping
            bool supportsExternalTrigger;       // Device supports external triggering
            bool supportsMultipleStreams;       // Device supports multiple streams
            bool supportsProgrammableRoi;       // Device supports programmable ROI
            std::vector<std::string> supportedPixelFormats; // Supported pixel formats
            std::vector<DeviceFeature> supportedFeatures;   // Other supported features

            // Device-specific information
            std::map<std::string, std::string> deviceInfo;
        };

        /**
         * @brief Get the device ID
         * @return Device ID string
         */
        [[nodiscard]] virtual std::string getDeviceId() const = 0;

        /**
         * @brief Get the device name
         * @return Human-readable device name
         */
        [[nodiscard]] virtual std::string getDeviceName() const = 0;

        /**
         * @brief Get the device model
         * @return Device model information
         */
        [[nodiscard]] virtual std::string getDeviceModel() const = 0;

        /**
         * @brief Initialize the device with specific configuration
         * @param config Configuration parameters
         * @return Status code indicating success or failure
         */
        virtual Status initialize(const Config &config) = 0;

        /**
         * @brief Start capturing frames
         * @param frameCallback Callback function that receives captured frames
         * @return Status code indicating success or failure
         */
        virtual Status startCapture(
            std::function<void(std::shared_ptr<Frame>)> frameCallback) = 0;

        /**
         * @brief Stop capturing frames
         * @return Status code indicating success or failure
         */
        virtual Status stopCapture() = 0;

        /**
         * @brief Check if the device is currently capturing
         * @return true if capturing, false otherwise
         */
        [[nodiscard]] virtual bool isCapturing() const = 0;

        /**
         * @brief Get supported configurations for this device
         * @return Vector of supported configurations
         */
        [[nodiscard]] virtual std::vector<Config> getSupportedConfigurations() const = 0;

        /**
         * @brief Get the current configuration
         * @return Current active configuration
         */
        [[nodiscard]] virtual Config getCurrentConfiguration() const = 0;

        /**
         * @brief Get device capabilities
         * @return Structure describing device capabilities
         */
        [[nodiscard]] virtual Capabilities getCapabilities() const = 0;

        /**
         * @brief Check if a specific feature is supported
         * @param feature Feature to check
         * @return true if the feature is supported
         */
        [[nodiscard]] virtual bool supportsFeature(DeviceFeature feature) const = 0;

        /**
         * @brief Configure external memory for zero-copy operations
         * @param externalMemory Pointer to external memory
         * @param size Size of the external memory buffer
         * @return Status code indicating success or failure
         */
        virtual Status setExternalMemory(void* externalMemory, size_t size) = 0;

        /**
         * @brief Configure direct output to shared memory
         * @param sharedMemoryName Name of the shared memory region
         * @return Status code indicating success or failure
         */
        virtual Status setDirectOutputToSharedMemory(const std::string& sharedMemoryName) = 0;

        /**
         * @brief Get the current frame rate
         * @return Current frames per second
         */
        [[nodiscard]] virtual double getCurrentFrameRate() const = 0;

        /**
         * @brief Get device-specific diagnostics
         * @return Map of diagnostic name to value
         */
        [[nodiscard]] virtual std::map<std::string, std::string> getDiagnostics() const = 0;

        /**
         * @brief Destructor
         */
        virtual ~UltrasoundDevice() = default;
    };
}