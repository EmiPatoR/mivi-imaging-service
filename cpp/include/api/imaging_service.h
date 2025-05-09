#pragma once

#include <memory>
#include <string>
#include <functional>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <deque>
#include <future>

#include "device/device_manager.h"
#include "frame/frame.h"
#include "communication/shared_memory.h"

namespace medical::imaging {
    /**
     * @class ImagingService
     * @brief Main service class for ultrasound imaging acquisition
     *
     * This class focuses exclusively on device management and frame acquisition,
     * writing frames to shared memory as quickly as possible with minimal processing.
     */
    class ImagingService {
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
            BlackmagicDevice::Config deviceConfig;  // Device-specific configuration

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

            // Constructor with default values
            Config() : deviceId(""),
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
                       performanceLogIntervalMs(5000) {
            }
        };

        /**
         * @brief Performance metrics for the imaging service
         */
        struct PerformanceMetrics {
            uint64_t frameCount;       // Total frames captured
            uint64_t droppedFrames;    // Frames dropped
            double averageFps;         // Average frames per second
            double currentFps;         // Current frames per second
            double averageLatencyMs;   // Average latency in milliseconds
            double maxLatencyMs;       // Maximum latency in milliseconds
            double cpuUsagePercent;    // CPU usage percentage
            double memoryUsageMb;      // Memory usage in MB
            std::chrono::seconds uptime; // Service uptime
        };

        /**
         * @brief Constructor
         */
        ImagingService();

        /**
         * @brief Destructor
         */
        ~ImagingService();

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
         * @brief Register a callback for device plug/unplug events
         * @param callback Function to call when device availability changes
         * @return Subscription ID
         */
        static int registerDeviceChangeCallback(
            const std::function<void(const std::string &, bool)> &callback);

        /**
         * @brief Unregister a device change callback
         * @param subscriptionId Subscription ID from registerDeviceChangeCallback
         * @return true if successful, false otherwise
         */
        static bool unregisterDeviceChangeCallback(int subscriptionId);

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

    private:
        // Internal methods
        void captureThread();
        void handleNewFrame(const std::shared_ptr<Frame> &frame);
        void performanceMonitorThread();

        // Utility methods
        Status setupDevice();
        Status setupSharedMemory();
        void updatePerformanceMetrics();
        bool setThreadPriority(std::thread &thread, bool isRealtime, int priority = 0);
        bool setThreadAffinity(std::thread &thread, int cpuCore);

        // Member variables
        std::atomic<bool> isInitialized_;
        std::atomic<bool> isRunning_;
        std::atomic<bool> stopRequested_;

        Config config_;

        std::shared_ptr<BlackmagicDevice> device_;
        std::shared_ptr<SharedMemory> sharedMemory_;

        std::function<void(std::shared_ptr<Frame>)> frameCallback_;

        // Threading
        std::thread captureThread_;
        std::thread performanceThread_;
        std::mutex mutex_;

        // Frame buffer for minimal buffering
        std::vector<std::shared_ptr<Frame>> frameBuffer_;
        size_t frameBufferHead_;
        size_t frameBufferTail_;
        std::mutex frameBufferMutex_;
        std::condition_variable frameCondition_;

        // Performance monitoring
        std::atomic<uint64_t> frameCount_;
        std::atomic<uint64_t> droppedFrames_;
        std::chrono::system_clock::time_point startTime_;
        std::chrono::system_clock::time_point lastFrameTime_;
        std::deque<double> fpsHistory_;
        std::deque<double> latencyHistory_;
        mutable std::mutex metricsMutex_;
        PerformanceMetrics metrics_{};
    };

    // Helper to get a global instance
    class ImagingServiceManager {
    public:
        static ImagingServiceManager &getInstance();

        std::shared_ptr<ImagingService> createService(const std::string &serviceName = "default");

        std::shared_ptr<ImagingService> getService(const std::string &serviceName = "default");

        bool destroyService(const std::string &serviceName);

        void destroyAll();

    private:
        ImagingServiceManager() = default;

        ~ImagingServiceManager();

        std::mutex mutex_;
        std::unordered_map<std::string, std::shared_ptr<ImagingService>> services_;
    };
} // namespace medical::imaging