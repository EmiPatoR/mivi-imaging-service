#pragma once

#include <memory>
#include <string>
#include <functional>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>

#include "device/device_manager.h"
#include "frame/frame.h"
#include "frame/frame_processor.h"
#include "communication/shared_memory.h"
#include "communication/grpc_server.h"

namespace medical {
namespace imaging {

/**
 * @class ImagingService
 * @brief Main service class for ultrasound imaging
 * 
 * This class coordinates all aspects of ultrasound imaging, including
 * device management, frame processing, and communication with other services.
 * It serves as the main entry point for applications using the imaging functionality.
 */
class ImagingService {
public:
    /**
     * @brief Status codes for service operations
     */
    enum class Status {
        OK,                     // Operation completed successfully
        DEVICE_ERROR,           // Error with imaging device
        PROCESSING_ERROR,       // Error processing frames
        COMMUNICATION_ERROR,    // Error in communication
        NOT_INITIALIZED,        // Service not initialized
        ALREADY_RUNNING,        // Service already running
        INVALID_ARGUMENT,       // Invalid argument provided
        NOT_RUNNING,            // Service not running
        INTERNAL_ERROR          // Unspecified internal error
    };
    
    /**
     * @brief Service configuration
     */
    struct Config {
        // Device settings
        std::string deviceId;               // ID of device to use, empty for auto-select
        UltrasoundDevice::Config deviceConfig; // Device-specific configuration
        
        // Processing settings
        bool enableSegmentation;            // Enable thyroid segmentation
        bool enableCalibration;             // Enable probe calibration
        int processingThreads;              // Number of processing threads
        
        // Communication settings
        bool enableSharedMemory;            // Enable shared memory communication
        std::string sharedMemoryName;       // Name of shared memory region
        size_t sharedMemorySize;            // Size of shared memory region
        
        bool enableGrpc;                    // Enable gRPC communication
        std::string grpcServerAddress;      // gRPC server address
        int grpcServerPort;                 // gRPC server port
        
        // QoS settings
        int frameBufferSize;                // Size of frame buffer
        
        // Constructor with default values
        Config() : 
            deviceId(""),
            enableSegmentation(true),
            enableCalibration(true),
            processingThreads(2),
            enableSharedMemory(true),
            sharedMemoryName("ultrasound_frames"),
            sharedMemorySize(64 * 1024 * 1024), // 64 MB
            enableGrpc(true),
            grpcServerAddress("0.0.0.0"),
            grpcServerPort(50051),
            frameBufferSize(120) {}
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
    Status initialize(const Config& config);
    
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
     * @brief Set a callback to be notified on new frames
     * @param callback Function to call with each new frame
     * @return Status code indicating success or failure
     */
    Status setFrameCallback(const std::function<void(std::shared_ptr<Frame>)> &callback);
    
    /**
     * @brief Get statistics about the service operation
     * @return Key-value pairs of statistics
     */
    std::map<std::string, std::string> getStatistics() const;
    
    /**
     * @brief Get available devices
     * @return List of available device IDs
     */
    static std::vector<std::string> getAvailableDevices() ;
    
    /**
     * @brief Register a callback for device plug/unplug events
     * @param callback Function to call when device availability changes
     * @return Subscription ID
     */
    static int registerDeviceChangeCallback(
        const std::function<void(const std::string&, bool)> &callback);
    
    /**
     * @brief Unregister a device change callback
     * @param subscriptionId Subscription ID from registerDeviceChangeCallback
     * @return true if successful, false otherwise
     */
    static bool unregisterDeviceChangeCallback(int subscriptionId);
    
private:
    // Internal methods
    void frameProcessingThread();
    void handleNewFrame(const std::shared_ptr<Frame>& frame);
    
    // Member variables
    std::atomic<bool> isInitialized_;
    std::atomic<bool> isRunning_;
    std::atomic<bool> stopRequested_;
    
    Config config_;
    
    std::shared_ptr<UltrasoundDevice> device_;
    std::unique_ptr<FrameProcessor> frameProcessor_;
    std::unique_ptr<SharedMemory> sharedMemory_;
    std::unique_ptr<GrpcServer> grpcServer_;
    
    std::function<void(std::shared_ptr<Frame>)> frameCallback_;
    
    std::thread processingThread_;
    std::mutex mutex_;
    std::condition_variable frameCondition_;
    
    std::vector<std::shared_ptr<Frame>> frameBuffer_;
    size_t frameBufferHead_;
    size_t frameBufferTail_;
    std::mutex frameBufferMutex_;
    
    // Statistics
    std::atomic<uint64_t> frameCount_;
    std::atomic<uint64_t> droppedFrames_;
    std::chrono::system_clock::time_point startTime_;
};

} // namespace imaging
} // namespace medical