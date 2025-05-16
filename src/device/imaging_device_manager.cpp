#include "device/imaging_device_adapter.h"
#include "device/device_adapters.h"
#include "device/device_manager.h"
#include "device/webcam_device.h"
#include <iostream>

namespace medical::imaging {

ImagingDeviceManager& ImagingDeviceManager::getInstance() {
    static ImagingDeviceManager instance;
    return instance;
}

ImagingDeviceManager::ImagingDeviceManager() {
    // Initial device discovery
    discoverDevices();
}

ImagingDeviceManager::~ImagingDeviceManager() = default;

int ImagingDeviceManager::discoverDevices() {
    std::cout << "Discovering all imaging devices..." << std::endl;

    std::lock_guard<std::mutex> lock(mutex_);
    devices_.clear();
    deviceInfo_.clear();

    // Discover Blackmagic devices
    int blackmagicCount = discoverBlackmagicDevices();

    // Discover webcam devices
    int webcamCount = discoverWebcamDevices();

    std::cout << "Total devices found: " << (blackmagicCount + webcamCount) << std::endl;
    return blackmagicCount + webcamCount;
}

int ImagingDeviceManager::discoverBlackmagicDevices() {
    std::cout << "Discovering Blackmagic devices..." << std::endl;

    // Get Blackmagic device manager
    auto &bmDeviceManager = DeviceManager::getInstance();
    auto deviceIds = bmDeviceManager.getAvailableDeviceIds();

    int count = 0;
    for (const auto &id : deviceIds) {
        // Get the device
        auto device = bmDeviceManager.getDevice(id);
        if (!device) {
            continue;
        }

        // Create adapter
        auto adapter = std::make_shared<BlackmagicDeviceAdapter>(device);

        // Store in our map
        devices_[id] = adapter;

        // Store device info
        ImagingDeviceAdapter::DeviceInfo info;
        info.id = id;
        info.name = device->getDeviceName();
        info.model = device->getDeviceModel();
        info.type = ImagingDeviceAdapter::DeviceType::BLACKMAGIC;
        deviceInfo_[id] = info;

        count++;
    }

    std::cout << "Found " << count << " Blackmagic devices" << std::endl;
    return count;
}

int ImagingDeviceManager::discoverWebcamDevices() {
    std::cout << "Discovering webcam devices..." << std::endl;

    // Get webcam device manager
    auto &webcamDeviceManager = WebcamDeviceManager::getInstance();
    auto deviceIds = webcamDeviceManager.getAvailableDeviceIds();

    int count = 0;
    for (const auto &id : deviceIds) {
        // Get the device
        auto device = webcamDeviceManager.getDevice(id);
        if (!device) {
            continue;
        }

        // Create adapter
        auto adapter = std::make_shared<WebcamDeviceAdapter>(device);

        // Store in our map
        devices_[id] = adapter;

        // Store device info
        ImagingDeviceAdapter::DeviceInfo info;
        info.id = id;
        info.name = device->getDeviceName();
        info.model = device->getDeviceModel();
        info.type = ImagingDeviceAdapter::DeviceType::WEBCAM;
        deviceInfo_[id] = info;

        count++;
    }

    std::cout << "Found " << count << " webcam devices" << std::endl;
    return count;
}

std::vector<std::string> ImagingDeviceManager::getAvailableDeviceIds() const {
    std::vector<std::string> ids;

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &pair : devices_) {
        ids.push_back(pair.first);
    }

    return ids;
}

std::shared_ptr<ImagingDeviceAdapter> ImagingDeviceManager::getDevice(const std::string &deviceId) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = devices_.find(deviceId);
    if (it != devices_.end()) {
        return it->second;
    }

    return nullptr;
}

std::vector<ImagingDeviceAdapter::DeviceInfo> ImagingDeviceManager::getAvailableDeviceInfo() const {
    std::vector<ImagingDeviceAdapter::DeviceInfo> infoList;

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &pair : deviceInfo_) {
        infoList.push_back(pair.second);
    }

    return infoList;
}

std::vector<std::string> ImagingDeviceManager::getDevicesByType(ImagingDeviceAdapter::DeviceType type) const {
    std::vector<std::string> ids;

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &pair : deviceInfo_) {
        if (pair.second.type == type) {
            ids.push_back(pair.first);
        }
    }

    return ids;
}

} // namespace medical::imaging