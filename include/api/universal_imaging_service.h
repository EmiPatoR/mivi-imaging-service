#pragma once

#include "api/imaging_service.h"
#include "api/webcam_imaging_service.h"
#include "device/imaging_device_adapter.h"

namespace medical::imaging {

/**
 * @class UniversalImagingService
 * @brief Provides a unified interface for both Blackmagic and webcam-based imaging services
 *
 * This class automatically selects the appropriate service implementation based on
 * the available devices, preferring Blackmagic devices when available.
 */
class UniversalImagingService {
public:
    /**
     * @brief Status codes for service operations
     */
    enum class Status {
        OK,                  // Operation completed successfully
        DEVICE_ERROR,        // Error with imaging device
        PROCESSING_ERROR,    // Error processing frames
        COMMUNICATION_ERROR, // Error in communication
        NOT_INITIALIZED,     // Service not initialized
        ALREADY_RUNNING,     // Service already running
        INVALID_ARGUMENT,    // Invalid argument provided
        NOT_RUNNING,         // Service not running
        INTERNAL_ERROR,      // Unspecified internal error
        TIMEOUT              // Operation timed out
    };

    /**
     * @brief Service configuration
     */
    struct Config {
        // Device settings
        std::string deviceId;                   // ID of device to use, empty for auto-select
        bool preferWebcams;                     // Whether to prefer webcams over Blackmagic devices
        ImagingDeviceAdapter::Config deviceConfig;  // Device-specific configuration

        // Performance settings
        bool enableDirectMemoryAccess; // Enable DMA if supported
        bool useRealtimePriority;      // Use realtime thread priority
        int threadAffinity;            // Thread CPU affinity (-1 for auto)
        bool pinMemory;                // Pin memory to RAM (prevent swapping)

        // Shared memory settings
        bool enableSharedMemory;       // Enable shared memory communication
        std::string sharedMemoryName;  // Name of shared memory region
        size_t sharedMemorySize;       // Size of shared memory region
        SharedMemoryType sharedMemoryType; // Type of shared memory implementation

        // Framebuffer settings
        int frameBufferSize;         // Number of frames to buffer
        bool dropFramesWhenFull;     // Whether to drop frames when buffer is full

        // Diagnostics
        bool enablePerformanceMonitoring; // Track detailed performance metrics
        bool logPerformanceStats;       // Log performance stats periodically
        int performanceLogIntervalMs;   // Interval for logging performance
        size_t maxFrameSize;          // Maximum size of a single frame in bytes

        // Constructor with default values
        Config() : deviceId(""),
                   preferWebcams(false),
                   enableDirectMemoryAccess(true),
                   useRealtimePriority(true),
                   threadAffinity(-1),
                   pinMemory(true),
                   enableSharedMemory(true),
                   sharedMemoryName("ultrasound_frames"),
                   sharedMemorySize(128 * 1024 * 1024), // 128 MB
                   sharedMemoryType(SharedMemoryType::MEMORY_MAPPED_FILE),
                   frameBufferSize(120),
                   dropFramesWhenFull(true),
                   enablePerformanceMonitoring(true),
                   logPerformanceStats(false),
                   performanceLogIntervalMs(5000),
                   maxFrameSize(1024 * 1024 * 8) {
        }
    };

    /**
     * @brief Performance metrics for the imaging service
     */
    using PerformanceMetrics = ImagingService::PerformanceMetrics;

    /**
     * @brief Constructor
     */
    UniversalImagingService();

    /**
     * @brief Destructor
     */
    ~UniversalImagingService();

    /**
     * @brief Initialize the service with the provided configuration
     * @param config Service configuration
     * @return Status code indicating success or failure
     */
    Status initialize(const Config &config);

    /**
     * @brief Start the imaging service
     * @return Status code indicating success or failure
     */
    Status start();

    /**
     * @brief Stop the imaging service
     * @return Status code indicating success or failure
     */
    Status stop();

    /**
     * @brief Check if the service is currently running
     * @return true if running, false otherwise
     */
    bool isRunning() const;

    /**
     * @brief Get the current configuration
     * @return Current service configuration
     */
    Config getConfig() const;

    /**
     * @brief Set the configuration (if not running)
     * @param config New configuration
     * @return Status code indicating success or failure
     */
    Status setConfig(const Config &config);

    /**
     * @brief Set a callback to be notified on new frames
     * @param callback Function to call with each new frame
     * @return Status code indicating success or failure
     */
    Status setFrameCallback(const std::function<void(std::shared_ptr<Frame>)> &callback);

    /**
     * @brief Get performance metrics about the service operation
     * @return Performance metrics structure
     */
    PerformanceMetrics getPerformanceMetrics() const;

    /**
     * @brief Reset performance metrics
     */
    void resetPerformanceMetrics();

    /**
     * @brief Get detailed statistics as key-value pairs
     * @return Map of statistic name to value
     */
    std::map<std::string, std::string> getStatistics() const;

    /**
     * @brief Get available devices
     * @return List of available device IDs
     */
    static std::vector<std::string> getAvailableDevices();

    /**
     * @brief Get available devices grouped by type
     * @return Map of device type to list of device IDs
     */
    static std::map<ImagingDeviceAdapter::DeviceType, std::vector<std::string>> getAvailableDevicesByType();

    /**
     * @brief Get information about all available devices
     * @return Vector of device information structures
     */
    static std::vector<ImagingDeviceAdapter::DeviceInfo> getAvailableDeviceInfo();

    /**
     * @brief Get the shared memory interface
     * @return Shared pointer to the shared memory interface
     */
    std::shared_ptr<SharedMemory> getSharedMemory() const;

    /**
     * @brief Dump diagnostic information to a file
     * @param filePath Path to the output file
     * @return true if successful, false otherwise
     */
    bool dumpDiagnostics(const std::string &filePath) const;

    /**
     * @brief Get the active service type
     * @return Device type of the active service
     */
    ImagingDeviceAdapter::DeviceType getActiveServiceType() const;

private:
    // Convert between status codes
    static Status convertStatus(ImagingService::Status status);
    static Status convertStatus(WebcamImagingService::Status status);

    // Convert between configurations
    static ImagingService::Config convertConfig(const Config &config);
    static WebcamImagingService::Config convertConfigWebcam(const Config &config);

    // Member variables
    Config config_;
    ImagingDeviceAdapter::DeviceType activeServiceType_;
    
    // Service implementations
    std::shared_ptr<ImagingService> blackmagicService_;
    std::shared_ptr<WebcamImagingService> webcamService_;
    
    // Callback
    std::function<void(std::shared_ptr<Frame>)> frameCallback_;
};

} // namespace medical::imaging