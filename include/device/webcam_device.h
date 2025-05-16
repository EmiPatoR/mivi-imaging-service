#pragma once

#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <deque>
#include <map>
#include <opencv2/opencv.hpp>

#include "frame/frame.h"

namespace medical {
namespace imaging {

/**
 * @class WebcamDevice
 * @brief Implementation for interfacing with webcam capture devices
 *
 * This class provides webcam capture functionality when Blackmagic
 * devices are not available. It uses OpenCV to access the webcam.
 */
class WebcamDevice {
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
     * @brief Configuration for device initialization
     */
    struct Config {
        int width;                      // Desired capture width
        int height;                     // Desired capture height
        double frameRate;               // Desired frame rate
        std::string pixelFormat;        // Desired pixel format ("RGB", "BGRA", "YUV")
        bool enableAudio;               // Whether to capture audio as well (not supported)
        int deviceIndex;                // Index of the webcam to use (0, 1, 2, etc.)

        // Constructor with default values
        Config() :
            width(1280),
            height(720),
            frameRate(30.0),
            pixelFormat("RGB"),
            enableAudio(false),
            deviceIndex(0) {}
    };

    /**
     * @brief Device capabilities
     */
    struct Capabilities {
        std::vector<std::string> supportedPixelFormats;
        std::vector<std::pair<int, int>> supportedResolutions;
        std::vector<double> supportedFrameRates;
        std::map<std::string, std::string> deviceInfo;
    };

    /**
     * @brief Constructor
     * @param deviceIndex Index of the webcam to use
     */
    explicit WebcamDevice(int deviceIndex);

    /**
     * @brief Destructor
     */
    ~WebcamDevice();

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
    // Private methods
    void captureThread();
    bool queryCapabilities();
    void updatePerformanceMetrics();
    std::shared_ptr<Frame> convertOpenCVMatToFrame(const cv::Mat &mat);

    // Member variables
    std::string deviceId_;
    std::string deviceName_;
    std::string deviceModel_;
    int deviceIndex_;

    Config currentConfig_;
    Capabilities capabilities_;
    std::function<void(std::shared_ptr<Frame>)> frameCallback_;

    cv::VideoCapture camera_;
    std::atomic<bool> isCapturing_;
    std::atomic<bool> stopRequested_;
    std::thread captureThread_;

    std::atomic<uint64_t> frameCount_;
    std::atomic<uint64_t> droppedFrames_;
    std::chrono::steady_clock::time_point startTime_;
    std::chrono::steady_clock::time_point lastFrameTime_;
    mutable std::mutex metricsMutex_;
    std::deque<double> fpsHistory_;
};

/**
 * @class WebcamDeviceManager
 * @brief Manages webcam devices for the application
 */
class WebcamDeviceManager {
public:
    static WebcamDeviceManager& getInstance();

    /**
     * @brief Discover available webcam devices
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
    std::shared_ptr<WebcamDevice> getDevice(const std::string &deviceId) const;

private:
    WebcamDeviceManager();
    ~WebcamDeviceManager();

    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<WebcamDevice>> devices_;
};

} // namespace imaging
} // namespace medical