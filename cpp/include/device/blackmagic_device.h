#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>
#include <deque>
#include <chrono>
#include <map>
#include <unordered_map>

#include "frame/frame.h"

// Forward declarations for Blackmagic SDK classes
class IDeckLink;
class IDeckLinkInput;
class IDeckLinkConfiguration;
class IDeckLinkDisplayMode;
class IDeckLinkVideoInputFrame;
class IDeckLinkAudioInputPacket;
class IDeckLinkProfileManager;
class IDeckLinkStatus;

namespace medical {
namespace imaging {

/**
 * @class BlackmagicDevice
 * @brief Enhanced implementation of UltrasoundDevice for Blackmagic capture devices
 *
 * This class provides concrete implementation for interfacing with
 * Blackmagic capture devices, with performance optimizations and
 * zero-copy support where possible.
 */
class BlackmagicDevice {
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
        INTERNAL_ERROR,         // Unspecified internal error
        INVALID_ARGUMENT        // Invalid argument provided
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
        std::unordered_map<std::string, std::string> deviceInfo;
    };

    /**
     * @brief Constructor
     * @param deckLink Pointer to the DeckLink device
     */
    explicit BlackmagicDevice(void* deckLink);

    /**
     * @brief Destructor
     */
    ~BlackmagicDevice();

    /**
     * @brief Get the device ID
     * @return Device ID string
     */
    std::string getDeviceId() const;

    /**
     * @brief Get the device name
     * @return Human-readable device name
     */
    std::string getDeviceName() const;

    /**
     * @brief Get the device model
     * @return Device model information
     */
    std::string getDeviceModel() const;

    /**
     * @brief Initialize the device with specific configuration
     * @param config Configuration parameters
     * @return Status code indicating success or failure
     */
    Status initialize(const Config &config);

    /**
     * @brief Start capturing frames
     * @param frameCallback Callback function that receives captured frames
     * @return Status code indicating success or failure
     */
    Status startCapture(std::function<void(std::shared_ptr<Frame>)> frameCallback);

    /**
     * @brief Stop capturing frames
     * @return Status code indicating success or failure
     */
    Status stopCapture();

    /**
     * @brief Check if the device is currently capturing
     * @return true if capturing, false otherwise
     */
    bool isCapturing() const;

    /**
     * @brief Get supported configurations for this device
     * @return Vector of supported configurations
     */
    std::vector<Config> getSupportedConfigurations() const;

    /**
     * @brief Get the current configuration
     * @return Current active configuration
     */
    Config getCurrentConfiguration() const;

    /**
     * @brief Get device capabilities
     * @return Structure describing device capabilities
     */
    Capabilities getCapabilities() const;

    /**
     * @brief Check if a specific feature is supported
     * @param feature Feature to check
     * @return true if the feature is supported
     */
    bool supportsFeature(DeviceFeature feature) const;

    /**
     * @brief Configure external memory for zero-copy operations
     * @param externalMemory Pointer to external memory
     * @param size Size of the external memory buffer
     * @return Status code indicating success or failure
     */
    Status setExternalMemory(void* externalMemory, size_t size);

    /**
     * @brief Configure direct output to shared memory
     * @param sharedMemoryName Name of the shared memory region
     * @return Status code indicating success or failure
     */
    Status setDirectOutputToSharedMemory(const std::string& sharedMemoryName);

    /**
     * @brief Get the current frame rate
     * @return Current frames per second
     */
    double getCurrentFrameRate() const;

    /**
     * @brief Get device-specific diagnostics
     * @return Map of diagnostic name to value
     */
    std::map<std::string, std::string> getDiagnostics() const;

private:
    // Inner callback class for DeckLink SDK
    class InputCallback;
    friend class InputCallback;

    // Zero-copy buffer management
    struct Buffer {
        void* memory;
        size_t size;
        bool inUse;
        std::chrono::steady_clock::time_point lastUsed;

        // For external memory tracking
        bool isExternal;
        size_t offset;  // Offset in shared memory or external buffer
    };

    // Convert between Blackmagic types and our types
    static uint32_t getBlackmagicPixelFormat(const std::string& format);
    static std::string getPixelFormatString(uint32_t blackmagicFormat);

    // Enhanced frame conversion with zero-copy support
    std::shared_ptr<Frame> convertFrame(IDeckLinkVideoInputFrame* videoFrame,
                                       IDeckLinkAudioInputPacket* audioPacket);

    // Advanced frame conversion with external memory
    std::shared_ptr<Frame> convertFrameExternalMemory(IDeckLinkVideoInputFrame* videoFrame,
                                                     IDeckLinkAudioInputPacket* audioPacket);

    // Find a compatible display mode
    bool findMatchingDisplayMode(const Config& config, IDeckLinkDisplayMode** mode);

    // Initialize DMA if supported
    bool initializeDMA();

    // Initialize direct-to-GPU if supported
    bool initializeGpuDirect();

    // Initialize buffer pool for zero-copy
    bool initializeBufferPool(size_t bufferCount, size_t bufferSize);

    // Allocate a buffer from the pool
    Buffer* allocateBuffer(size_t size);

    // Release a buffer back to the pool
    void releaseBuffer(Buffer* buffer);

    // Query device capabilities
    void queryCapabilities();

    // Monitor performance metrics
    void updatePerformanceMetrics(IDeckLinkVideoInputFrame* videoFrame);

    // Member variables
    IDeckLink* deckLink_;
    IDeckLinkInput* deckLinkInput_;
    IDeckLinkConfiguration* deckLinkConfig_;
    IDeckLinkProfileManager* profileManager_;
    IDeckLinkStatus* status_;
    std::shared_ptr<InputCallback> callback_;

    Config currentConfig_;
    Capabilities capabilities_;
    std::function<void(std::shared_ptr<Frame>)> frameCallback_;

    // External memory management
    void* externalMemory_;
    size_t externalMemorySize_;
    std::string directSharedMemoryName_;

    // Buffer pool for zero-copy operations
    std::vector<Buffer> bufferPool_;
    std::mutex bufferPoolMutex_;

    // Performance monitoring
    std::atomic<uint64_t> frameCount_;
    std::atomic<uint64_t> droppedFrames_;
    std::chrono::steady_clock::time_point startTime_;
    std::chrono::steady_clock::time_point lastFrameTime_;
    mutable std::mutex metricsMutex_;
    std::deque<double> fpsHistory_;

    // State variables
    std::atomic<bool> isCapturing_;
    std::atomic<bool> isDmaEnabled_;
    std::atomic<bool> isGpuDirectEnabled_;
    mutable std::mutex mutex_;
    std::string deviceId_;
    std::string deviceName_;
    std::string deviceModel_;
};

// Simplified device manager for handling Blackmagic devices
class BlackmagicDeviceManager {
public:
    static BlackmagicDeviceManager& getInstance();

    /**
     * @brief Discover available Blackmagic devices
     * @return Number of devices found
     */
    int discoverDevices();

    /**
     * @brief Get a list of all available device IDs
     * @return Vector of device IDs
     */
    std::vector<std::string> getAvailableDeviceIds() const;

    /**
     * @brief Get a device by its ID
     * @param deviceId The ID of the device to retrieve
     * @return Shared pointer to the device, or nullptr if not found
     */
    std::shared_ptr<BlackmagicDevice> getDevice(const std::string& deviceId) const;

    /**
     * @brief Register a callback for device hot-plug events
     * @param callback Function to call when devices are added or removed
     * @return Subscription ID that can be used to unregister the callback
     */
    int registerDeviceChangeCallback(
        std::function<void(const std::string& deviceId, bool added)> callback);

    /**
     * @brief Unregister a previously registered callback
     * @param subscriptionId The ID returned from registerDeviceChangeCallback
     * @return true if successfully unregistered, false otherwise
     */
    bool unregisterDeviceChangeCallback(int subscriptionId);

private:
    BlackmagicDeviceManager();
    ~BlackmagicDeviceManager();

    // Internal implementation
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace imaging
} // namespace medical