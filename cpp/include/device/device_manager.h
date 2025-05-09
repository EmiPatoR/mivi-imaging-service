#pragma once

#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <map>
#include <mutex>

#include "device/ultrasound_device.h"

namespace medical {
namespace imaging {

/**
 * @class DeviceManager
 * @brief Manages ultrasound imaging devices, handling discovery and lifecycle
 * 
 * This class is responsible for discovering, initializing, and managing
 * available ultrasound imaging devices in the system. It serves as the entry
 * point for applications to access imaging hardware.
 */
class DeviceManager {
public:
    /**
     * @brief Get the DeviceManager singleton instance
     * @return Reference to the DeviceManager instance
     */
    static DeviceManager& getInstance();

    /**
     * @brief Discover available imaging devices
     * @return Number of devices found
     */
    int discoverDevices();
    
    /**
     * @brief Get a list of all available device IDs
     * @return Vector of device IDs
     */
    std::vector<std::string> getAvailableDeviceIds();

    /**
     * @brief Get a device by its ID
     * @param deviceId The ID of the device to retrieve
     * @return Shared pointer to the device, or nullptr if not found
     */
    std::shared_ptr<UltrasoundDevice> getDevice(const std::string& deviceId);

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

    // Test method to add a device manually
    void addTestDevice(std::shared_ptr<UltrasoundDevice> device);

    // Prevent copy and assignment
    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

private:
    DeviceManager();  // Private constructor for singleton
    ~DeviceManager();

    class DeviceNotificationCallback;  // Forward declaration for BMD callback

    // Private implementation details
    std::map<std::string, std::shared_ptr<UltrasoundDevice>> devices_;
    std::map<int, std::function<void(const std::string&, bool)>> callbacks_;
    int nextCallbackId_;
    mutable std::mutex mutex_;
    std::shared_ptr<DeviceNotificationCallback> notificationCallback_;
    void* discoveryInstance_; // Opaque pointer to IDeckLinkDiscovery

    // Handle device arrival/removal
    void deviceArrived(void* deckLinkDevice);
    void deviceRemoved(void* deckLinkDevice);

    // Thread-safe way to add a device
    void addDeviceSafe(const std::string& deviceId, std::shared_ptr<UltrasoundDevice> device);

    // Thread-safe way to remove a device
    void removeDeviceSafe(const std::string& deviceId);
    
    // Friend the callback class so it can access private methods
    friend class DeviceNotificationCallback;
};

} // namespace imaging
} // namespace medical