#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <mutex>

#include "frame/frame.h"

namespace medical::imaging {

/**
 * @class ImagingDeviceAdapter
 * @brief Interface for imaging devices that abstracts between Blackmagic and webcam devices
 * 
 * This interface provides a common abstraction layer for both Blackmagic and webcam devices,
 * allowing the application to use either type without changing the rest of the code.
 */
class ImagingDeviceAdapter {
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
        std::string pixelFormat;        // Desired pixel format
        bool enableAudio;               // Whether to capture audio as well
        
        // Constructor with default values
        Config() :
            width(1920),
            height(1080),
            frameRate(30.0),
            pixelFormat("RGB"),
            enableAudio(false) {}
    };

    /**
     * @brief Device type enumeration
     */
    enum class DeviceType {
        BLACKMAGIC,
        WEBCAM,
        UNKNOWN
    };

    /**
     * @brief Device information structure
     */
    struct DeviceInfo {
        std::string id;
        std::string name;
        std::string model;
        DeviceType type;
    };

    /**
     * @brief Virtual destructor
     */
    virtual ~ImagingDeviceAdapter() = default;

    /**
     * @brief Get the device ID
     * @return Device ID string
     */
    virtual std::string getDeviceId() const = 0;

    /**
     * @brief Get the device name
     * @return Human-readable device name
     */
    virtual std::string getDeviceName() const = 0;

    /**
     * @brief Get the device model
     * @return Device model information
     */
    virtual std::string getDeviceModel() const = 0;

    /**
     * @brief Get the device type
     * @return Device type (Blackmagic, Webcam, etc.)
     */
    virtual DeviceType getDeviceType() const = 0;

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
    virtual Status startCapture(std::function<void(std::shared_ptr<Frame>)> frameCallback) = 0;

    /**
     * @brief Stop capturing frames
     * @return Status code indicating success or failure
     */
    virtual Status stopCapture() = 0;

    /**
     * @brief Check if the device is currently capturing
     * @return true if capturing, false otherwise
     */
    virtual bool isCapturing() const = 0;

    /**
     * @brief Get the current configuration
     * @return Current active configuration
     */
    virtual Config getCurrentConfiguration() const = 0;

    /**
     * @brief Get the current frame rate
     * @return Current frames per second
     */
    virtual double getCurrentFrameRate() const = 0;

    /**
     * @brief Get device-specific diagnostics
     * @return Map of diagnostic name to value
     */
    virtual std::map<std::string, std::string> getDiagnostics() const = 0;
};

/**
 * @class ImagingDeviceManager
 * @brief Manages imaging devices and provides a unified interface for both Blackmagic and webcam devices
 */
class ImagingDeviceManager {
public:
    static ImagingDeviceManager& getInstance();
    
    /**
     * @brief Discover available imaging devices (both Blackmagic and webcam)
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
     * @return Shared pointer to the device adapter, or nullptr if not found
     */
    std::shared_ptr<ImagingDeviceAdapter> getDevice(const std::string &deviceId) const;

    /**
     * @brief Get information about all available devices
     * @return Vector of device information structures
     */
    std::vector<ImagingDeviceAdapter::DeviceInfo> getAvailableDeviceInfo() const;

    /**
     * @brief Get devices of a specific type
     * @param type The type of devices to retrieve
     * @return Vector of device IDs matching the specified type
     */
    std::vector<std::string> getDevicesByType(ImagingDeviceAdapter::DeviceType type) const;

private:
    ImagingDeviceManager();
    ~ImagingDeviceManager();
    
    // Helper methods to discover different types of devices
    int discoverBlackmagicDevices();
    int discoverWebcamDevices();
    
    // Data structures
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<ImagingDeviceAdapter>> devices_;
    std::map<std::string, ImagingDeviceAdapter::DeviceInfo> deviceInfo_;
};

} // namespace medical::imaging