#include "device/device_manager.h"
#include <vector>
#include <string>


namespace medical::imaging {
    // This is a stub implementation for device discovery
    // The actual implementation would use various methods to discover connected devices

    class DeviceDiscovery {
    public:
        static std::vector<std::string> discoverDevices() {
            // Use the DeviceManager to discover devices
            auto &deviceManager = DeviceManager::getInstance();
            return deviceManager.getAvailableDeviceIds();
        }
    };
} // namespace medical::imaging
