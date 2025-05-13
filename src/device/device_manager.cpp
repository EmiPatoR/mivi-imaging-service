#include "device/device_manager.h"
#include "DeckLinkAPI.h"
#include "utils/refiid_compare.h"
#include <iostream>

namespace medical {
namespace imaging {

/**
 * @brief Implementation of IDeckLinkDeviceNotificationCallback for device hotplug
 */
class DeviceManager::DeviceNotificationCallback : public IDeckLinkDeviceNotificationCallback {
public:
    DeviceNotificationCallback(DeviceManager* manager) : manager_(manager), refCount_(1) {}

    // IDeckLinkDeviceNotificationCallback interface
    HRESULT STDMETHODCALLTYPE DeckLinkDeviceArrived(IDeckLink* deckLinkDevice) override {
        std::cout << "Device arrived callback" << std::endl;
        if (manager_ && deckLinkDevice) {
            manager_->deviceArrived(deckLinkDevice);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DeckLinkDeviceRemoved(IDeckLink* deckLinkDevice) override {
        std::cout << "Device removed callback" << std::endl;
        if (manager_ && deckLinkDevice) {
            manager_->deviceRemoved(deckLinkDevice);
        }
        return S_OK;
    }

    // IUnknown interface
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID* ppv) override {
        if (!ppv) {
            return E_POINTER;
        }

        if (iid == IID_IUnknown || iid == IID_IDeckLinkDeviceNotificationCallback) {
            *ppv = static_cast<IDeckLinkDeviceNotificationCallback*>(this);
            AddRef();
            return S_OK;
        }

        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refCount_;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG newRefValue = --refCount_;
        if (newRefValue == 0) {
            delete this;
        }
        return newRefValue;
    }

private:
    DeviceManager* manager_;
    std::atomic<ULONG> refCount_;
};

DeviceManager& DeviceManager::getInstance() {
    static DeviceManager instance;
    return instance;
}

DeviceManager::DeviceManager()
    : nextCallbackId_(0), discoveryInstance_(nullptr) {

    std::cout << "DeviceManager constructor" << std::endl;

    // Create notification callback
    notificationCallback_ = std::make_shared<DeviceNotificationCallback>(this);

    // Create discovery instance
    IDeckLinkDiscovery* discovery = CreateDeckLinkDiscoveryInstance();
    if (discovery) {
        std::cout << "Installing device notifications" << std::endl;
        discovery->InstallDeviceNotifications(notificationCallback_.get());
        discoveryInstance_ = discovery;
    } else {
        std::cout << "Failed to create discovery instance" << std::endl;
    }

    // Initial device discovery
    discoverDevices();
}

DeviceManager::~DeviceManager() {
    std::cout << "DeviceManager destructor" << std::endl;

    // Uninstall notifications
    if (discoveryInstance_) {
        IDeckLinkDiscovery* discovery = static_cast<IDeckLinkDiscovery*>(discoveryInstance_);
        discovery->UninstallDeviceNotifications();
        discovery->Release();
        discoveryInstance_ = nullptr;
    }

    // Clear devices
    std::unique_lock<std::mutex> lock(mutex_);
    devices_.clear();
}

int DeviceManager::discoverDevices() {
    std::cout << "Discovering devices" << std::endl;

    // Create an iterator
    IDeckLinkIterator* iterator = CreateDeckLinkIteratorInstance();
    if (!iterator) {
        std::cout << "Failed to create iterator" << std::endl;
        return 0;
    }

    int count = 0;
    IDeckLink* deckLink = nullptr;

    // Iterate through all devices
    std::cout << "Iterating through devices" << std::endl;
    while (iterator->Next(&deckLink) == S_OK) {
        if (deckLink) {
            std::cout << "Found device" << std::endl;

            // Process the device
            deviceArrived(deckLink);
            count++;
        }
    }

    iterator->Release();
    return count;
}

std::vector<std::string> DeviceManager::getAvailableDeviceIds() {
    std::vector<std::string> deviceIds;
    std::unique_lock<std::mutex> lock(mutex_);

    for (const auto& pair : devices_) {
        deviceIds.push_back(pair.first);
    }

    return deviceIds;
}

std::shared_ptr<BlackmagicDevice> DeviceManager::getDevice(const std::string& deviceId) {
    std::unique_lock<std::mutex> lock(mutex_);

    auto it = devices_.find(deviceId);
    if (it != devices_.end()) {
        return it->second;
    }

    return nullptr;
}

int DeviceManager::registerDeviceChangeCallback(
    std::function<void(const std::string& deviceId, bool added)> callback) {

    std::unique_lock<std::mutex> lock(mutex_);
    int callbackId = nextCallbackId_++;
    callbacks_[callbackId] = callback;
    return callbackId;
}

bool DeviceManager::unregisterDeviceChangeCallback(int subscriptionId) {
    std::unique_lock<std::mutex> lock(mutex_);

    auto it = callbacks_.find(subscriptionId);
    if (it != callbacks_.end()) {
        callbacks_.erase(it);
        return true;
    }

    return false;
}

void DeviceManager::addTestDevice(std::shared_ptr<BlackmagicDevice> device) {
    if (!device) {
        return;
    }

    std::string deviceId = device->getDeviceId();
    addDeviceSafe(deviceId, device);
}

void DeviceManager::deviceArrived(void* deckLinkDevice) {
    std::cout << "Device arrived" << std::endl;
    IDeckLink* deckLink = static_cast<IDeckLink*>(deckLinkDevice);
    if (!deckLink) {
        std::cout << "Null deckLink" << std::endl;
        return;
    }

    // Add a reference since we're storing it
    deckLink->AddRef();

    // Create device wrapper outside the lock
    auto device = std::make_shared<BlackmagicDevice>(deckLink);

    // Get the device ID
    std::string deviceId = device->getDeviceId();
    std::cout << "Created device with ID: " << deviceId << std::endl;

    // Add it to the map using our thread-safe method
    addDeviceSafe(deviceId, device);
}

void DeviceManager::deviceRemoved(void* deckLinkDevice) {
    std::cout << "Device removed" << std::endl;
    IDeckLink* deckLink = static_cast<IDeckLink*>(deckLinkDevice);
    if (!deckLink) {
        std::cout << "Null deckLink" << std::endl;
        return;
    }

    // Find the device in our map
    std::string deviceIdToRemove;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        for (auto it = devices_.begin(); it != devices_.end(); ++it) {
            auto blackmagicDevice = it->second;
            if (blackmagicDevice) {
                // Use the device ID which includes the pointer value
                if (blackmagicDevice->getDeviceId().find(
                    std::to_string(reinterpret_cast<uintptr_t>(deckLink)))
                    != std::string::npos) {
                    deviceIdToRemove = it->first;
                    break;
                }
            }
        }
    }

    // Remove it using our thread-safe method
    if (!deviceIdToRemove.empty()) {
        removeDeviceSafe(deviceIdToRemove);
    }
}

void DeviceManager::addDeviceSafe(const std::string& deviceId, std::shared_ptr<BlackmagicDevice> device) {
    // Local copies for callbacks
    std::vector<std::function<void(const std::string&, bool)>> callbacksToCall;

    // First update the device map under lock
    {
        std::lock_guard<std::mutex> guard(mutex_);

        // Store the device
        devices_[deviceId] = device;

        // Copy callbacks
        for (const auto& pair : callbacks_) {
            callbacksToCall.push_back(pair.second);
        }
    }

    // Now call callbacks outside the lock
    for (const auto& callback : callbacksToCall) {
        try {
            callback(deviceId, true);
        } catch (const std::exception& e) {
            std::cerr << "Exception in device callback: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown exception in device callback" << std::endl;
        }
    }
}

void DeviceManager::removeDeviceSafe(const std::string& deviceId) {
    // Local copies for callbacks
    std::vector<std::function<void(const std::string&, bool)>> callbacksToCall;

    // First update the device map under lock
    {
        std::lock_guard<std::mutex> guard(mutex_);

        // Remove the device
        devices_.erase(deviceId);

        // Copy callbacks
        for (const auto& pair : callbacks_) {
            callbacksToCall.push_back(pair.second);
        }
    }

    // Now call callbacks outside the lock
    for (const auto& callback : callbacksToCall) {
        try {
            callback(deviceId, false);
        } catch (const std::exception& e) {
            std::cerr << "Exception in device callback: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown exception in device callback" << std::endl;
        }
    }
}

} // namespace imaging
} // namespace medical