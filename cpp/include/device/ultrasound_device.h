#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>

#include "frame/frame.h"


namespace medical::imaging {
    /**
     * @class UltrasoundDevice
     * @brief Abstract base class representing an ultrasound imaging device
     *
     * This class defines the interface for all ultrasound imaging devices,
     * allowing the system to work with different hardware while maintaining
     * a consistent API.
     */
    class UltrasoundDevice {
    public:
        /**
         * @brief Status codes for device operations
         */
        enum class Status {
            OK, // Operation completed successfully
            DEVICE_NOT_FOUND, // Device not found or not connected
            INIT_FAILED, // Failed to initialize device
            ALREADY_STREAMING, // Device is already streaming
            NOT_STREAMING, // Device is not currently streaming
            CONFIGURATION_ERROR, // Invalid configuration
            INTERNAL_ERROR // Unspecified internal error
        };

        /**
         * @brief Configuration for device initialization
         */
        struct Config {
            int width; // Desired capture width
            int height; // Desired capture height
            double frameRate; // Desired frame rate
            std::string pixelFormat; // Desired pixel format
            bool enableAudio; // Whether to capture audio as well

            // Constructor with default values
            Config() : width(1920),
                       height(1080),
                       frameRate(60.0),
                       pixelFormat("YUV"),
                       enableAudio(false) {
            }
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
         * @brief Destructor
         */
        virtual ~UltrasoundDevice() = default;
    };
} // namespace medical::imaging
