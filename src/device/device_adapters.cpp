#include "device/device_adapters.h"
#include <iostream>

namespace medical::imaging {

// BlackmagicDeviceAdapter implementation
BlackmagicDeviceAdapter::BlackmagicDeviceAdapter(std::shared_ptr<BlackmagicDevice> device)
    : device_(std::move(device)) {
    if (device_) {
        // Initialize current config from device
        auto deviceConfig = device_->getCurrentConfiguration();
        currentConfig_.width = deviceConfig.width;
        currentConfig_.height = deviceConfig.height;
        currentConfig_.frameRate = deviceConfig.frameRate;
        currentConfig_.pixelFormat = deviceConfig.pixelFormat;
        currentConfig_.enableAudio = deviceConfig.enableAudio;
    }
}

std::string BlackmagicDeviceAdapter::getDeviceId() const {
    return device_ ? device_->getDeviceId() : "";
}

std::string BlackmagicDeviceAdapter::getDeviceName() const {
    return device_ ? device_->getDeviceName() : "";
}

std::string BlackmagicDeviceAdapter::getDeviceModel() const {
    return device_ ? device_->getDeviceModel() : "";
}

ImagingDeviceAdapter::DeviceType BlackmagicDeviceAdapter::getDeviceType() const {
    return DeviceType::BLACKMAGIC;
}

ImagingDeviceAdapter::Status BlackmagicDeviceAdapter::initialize(const Config &config) {
    if (!device_) {
        return Status::DEVICE_NOT_FOUND;
    }
    
    // Convert to device-specific config
    BlackmagicDevice::Config deviceConfig;
    deviceConfig.width = config.width;
    deviceConfig.height = config.height;
    deviceConfig.frameRate = config.frameRate;
    deviceConfig.pixelFormat = config.pixelFormat;
    deviceConfig.enableAudio = config.enableAudio;
    
    // Initialize the device
    auto status = device_->initialize(deviceConfig);
    
    // Map status codes
    switch (status) {
        case BlackmagicDevice::Status::OK:
            currentConfig_ = config;
            return Status::OK;
        case BlackmagicDevice::Status::DEVICE_NOT_FOUND:
            return Status::DEVICE_NOT_FOUND;
        case BlackmagicDevice::Status::INIT_FAILED:
            return Status::INIT_FAILED;
        case BlackmagicDevice::Status::ALREADY_STREAMING:
            return Status::ALREADY_STREAMING;
        case BlackmagicDevice::Status::NOT_STREAMING:
            return Status::NOT_STREAMING;
        case BlackmagicDevice::Status::CONFIGURATION_ERROR:
            return Status::CONFIGURATION_ERROR;
        case BlackmagicDevice::Status::FEATURE_NOT_SUPPORTED:
            return Status::FEATURE_NOT_SUPPORTED;
        case BlackmagicDevice::Status::PERMISSION_DENIED:
            return Status::PERMISSION_DENIED;
        case BlackmagicDevice::Status::TIMEOUT:
            return Status::TIMEOUT;
        case BlackmagicDevice::Status::IO_ERROR:
            return Status::IO_ERROR;
        case BlackmagicDevice::Status::INTERNAL_ERROR:
            return Status::INTERNAL_ERROR;
        case BlackmagicDevice::Status::INVALID_ARGUMENT:
            return Status::INVALID_ARGUMENT;
        default:
            return Status::INTERNAL_ERROR;
    }
}

ImagingDeviceAdapter::Status BlackmagicDeviceAdapter::startCapture(
    std::function<void(std::shared_ptr<Frame>)> frameCallback) {
    if (!device_) {
        return Status::DEVICE_NOT_FOUND;
    }
    
    // Start capture
    auto status = device_->startCapture(frameCallback);
    
    // Map status codes
    switch (status) {
        case BlackmagicDevice::Status::OK:
            return Status::OK;
        case BlackmagicDevice::Status::DEVICE_NOT_FOUND:
            return Status::DEVICE_NOT_FOUND;
        case BlackmagicDevice::Status::INIT_FAILED:
            return Status::INIT_FAILED;
        case BlackmagicDevice::Status::ALREADY_STREAMING:
            return Status::ALREADY_STREAMING;
        case BlackmagicDevice::Status::NOT_STREAMING:
            return Status::NOT_STREAMING;
        case BlackmagicDevice::Status::CONFIGURATION_ERROR:
            return Status::CONFIGURATION_ERROR;
        case BlackmagicDevice::Status::FEATURE_NOT_SUPPORTED:
            return Status::FEATURE_NOT_SUPPORTED;
        case BlackmagicDevice::Status::PERMISSION_DENIED:
            return Status::PERMISSION_DENIED;
        case BlackmagicDevice::Status::TIMEOUT:
            return Status::TIMEOUT;
        case BlackmagicDevice::Status::IO_ERROR:
            return Status::IO_ERROR;
        case BlackmagicDevice::Status::INTERNAL_ERROR:
            return Status::INTERNAL_ERROR;
        case BlackmagicDevice::Status::INVALID_ARGUMENT:
            return Status::INVALID_ARGUMENT;
        default:
            return Status::INTERNAL_ERROR;
    }
}

ImagingDeviceAdapter::Status BlackmagicDeviceAdapter::stopCapture() {
    if (!device_) {
        return Status::DEVICE_NOT_FOUND;
    }
    
    // Stop capture
    auto status = device_->stopCapture();
    
    // Map status codes
    switch (status) {
        case BlackmagicDevice::Status::OK:
            return Status::OK;
        case BlackmagicDevice::Status::DEVICE_NOT_FOUND:
            return Status::DEVICE_NOT_FOUND;
        case BlackmagicDevice::Status::INIT_FAILED:
            return Status::INIT_FAILED;
        case BlackmagicDevice::Status::ALREADY_STREAMING:
            return Status::ALREADY_STREAMING;
        case BlackmagicDevice::Status::NOT_STREAMING:
            return Status::NOT_STREAMING;
        case BlackmagicDevice::Status::CONFIGURATION_ERROR:
            return Status::CONFIGURATION_ERROR;
        case BlackmagicDevice::Status::FEATURE_NOT_SUPPORTED:
            return Status::FEATURE_NOT_SUPPORTED;
        case BlackmagicDevice::Status::PERMISSION_DENIED:
            return Status::PERMISSION_DENIED;
        case BlackmagicDevice::Status::TIMEOUT:
            return Status::TIMEOUT;
        case BlackmagicDevice::Status::IO_ERROR:
            return Status::IO_ERROR;
        case BlackmagicDevice::Status::INTERNAL_ERROR:
            return Status::INTERNAL_ERROR;
        case BlackmagicDevice::Status::INVALID_ARGUMENT:
            return Status::INVALID_ARGUMENT;
        default:
            return Status::INTERNAL_ERROR;
    }
}

bool BlackmagicDeviceAdapter::isCapturing() const {
    return device_ && device_->isCapturing();
}

ImagingDeviceAdapter::Config BlackmagicDeviceAdapter::getCurrentConfiguration() const {
    return currentConfig_;
}

double BlackmagicDeviceAdapter::getCurrentFrameRate() const {
    return device_ ? device_->getCurrentFrameRate() : 0.0;
}

std::map<std::string, std::string> BlackmagicDeviceAdapter::getDiagnostics() const {
    return device_ ? device_->getDiagnostics() : std::map<std::string, std::string>{};
}

// WebcamDeviceAdapter implementation
WebcamDeviceAdapter::WebcamDeviceAdapter(std::shared_ptr<WebcamDevice> device)
    : device_(std::move(device)) {
    if (device_) {
        // Initialize current config from device
        auto deviceConfig = device_->getCurrentConfiguration();
        currentConfig_.width = deviceConfig.width;
        currentConfig_.height = deviceConfig.height;
        currentConfig_.frameRate = deviceConfig.frameRate;
        currentConfig_.pixelFormat = deviceConfig.pixelFormat;
        currentConfig_.enableAudio = deviceConfig.enableAudio;
    }
}

std::string WebcamDeviceAdapter::getDeviceId() const {
    return device_ ? device_->getDeviceId() : "";
}

std::string WebcamDeviceAdapter::getDeviceName() const {
    return device_ ? device_->getDeviceName() : "";
}

std::string WebcamDeviceAdapter::getDeviceModel() const {
    return device_ ? device_->getDeviceModel() : "";
}

ImagingDeviceAdapter::DeviceType WebcamDeviceAdapter::getDeviceType() const {
    return DeviceType::WEBCAM;
}

ImagingDeviceAdapter::Status WebcamDeviceAdapter::initialize(const Config &config) {
    if (!device_) {
        return Status::DEVICE_NOT_FOUND;
    }
    
    // Convert to device-specific config
    WebcamDevice::Config deviceConfig;
    deviceConfig.width = config.width;
    deviceConfig.height = config.height;
    deviceConfig.frameRate = config.frameRate;
    deviceConfig.pixelFormat = config.pixelFormat;
    deviceConfig.enableAudio = config.enableAudio;
    
    // Initialize the device
    auto status = device_->initialize(deviceConfig);
    
    // Map status codes
    switch (status) {
        case WebcamDevice::Status::OK:
            currentConfig_ = config;
            return Status::OK;
        case WebcamDevice::Status::DEVICE_NOT_FOUND:
            return Status::DEVICE_NOT_FOUND;
        case WebcamDevice::Status::INIT_FAILED:
            return Status::INIT_FAILED;
        case WebcamDevice::Status::ALREADY_STREAMING:
            return Status::ALREADY_STREAMING;
        case WebcamDevice::Status::NOT_STREAMING:
            return Status::NOT_STREAMING;
        case WebcamDevice::Status::CONFIGURATION_ERROR:
            return Status::CONFIGURATION_ERROR;
        case WebcamDevice::Status::FEATURE_NOT_SUPPORTED:
            return Status::FEATURE_NOT_SUPPORTED;
        case WebcamDevice::Status::PERMISSION_DENIED:
            return Status::PERMISSION_DENIED;
        case WebcamDevice::Status::TIMEOUT:
            return Status::TIMEOUT;
        case WebcamDevice::Status::IO_ERROR:
            return Status::IO_ERROR;
        case WebcamDevice::Status::INTERNAL_ERROR:
            return Status::INTERNAL_ERROR;
        case WebcamDevice::Status::INVALID_ARGUMENT:
            return Status::INVALID_ARGUMENT;
        default:
            return Status::INTERNAL_ERROR;
    }
}

ImagingDeviceAdapter::Status WebcamDeviceAdapter::startCapture(
    std::function<void(std::shared_ptr<Frame>)> frameCallback) {
    if (!device_) {
        return Status::DEVICE_NOT_FOUND;
    }
    
    // Start capture
    auto status = device_->startCapture(frameCallback);
    
    // Map status codes
    switch (status) {
        case WebcamDevice::Status::OK:
            return Status::OK;
        case WebcamDevice::Status::DEVICE_NOT_FOUND:
            return Status::DEVICE_NOT_FOUND;
        case WebcamDevice::Status::INIT_FAILED:
            return Status::INIT_FAILED;
        case WebcamDevice::Status::ALREADY_STREAMING:
            return Status::ALREADY_STREAMING;
        case WebcamDevice::Status::NOT_STREAMING:
            return Status::NOT_STREAMING;
        case WebcamDevice::Status::CONFIGURATION_ERROR:
            return Status::CONFIGURATION_ERROR;
        case WebcamDevice::Status::FEATURE_NOT_SUPPORTED:
            return Status::FEATURE_NOT_SUPPORTED;
        case WebcamDevice::Status::PERMISSION_DENIED:
            return Status::PERMISSION_DENIED;
        case WebcamDevice::Status::TIMEOUT:
            return Status::TIMEOUT;
        case WebcamDevice::Status::IO_ERROR:
            return Status::IO_ERROR;
        case WebcamDevice::Status::INTERNAL_ERROR:
            return Status::INTERNAL_ERROR;
        case WebcamDevice::Status::INVALID_ARGUMENT:
            return Status::INVALID_ARGUMENT;
        default:
            return Status::INTERNAL_ERROR;
    }
}

ImagingDeviceAdapter::Status WebcamDeviceAdapter::stopCapture() {
    if (!device_) {
        return Status::DEVICE_NOT_FOUND;
    }
    
    // Stop capture
    auto status = device_->stopCapture();
    
    // Map status codes
    switch (status) {
        case WebcamDevice::Status::OK:
            return Status::OK;
        case WebcamDevice::Status::DEVICE_NOT_FOUND:
            return Status::DEVICE_NOT_FOUND;
        case WebcamDevice::Status::INIT_FAILED:
            return Status::INIT_FAILED;
        case WebcamDevice::Status::ALREADY_STREAMING:
            return Status::ALREADY_STREAMING;
        case WebcamDevice::Status::NOT_STREAMING:
            return Status::NOT_STREAMING;
        case WebcamDevice::Status::CONFIGURATION_ERROR:
            return Status::CONFIGURATION_ERROR;
        case WebcamDevice::Status::FEATURE_NOT_SUPPORTED:
            return Status::FEATURE_NOT_SUPPORTED;
        case WebcamDevice::Status::PERMISSION_DENIED:
            return Status::PERMISSION_DENIED;
        case WebcamDevice::Status::TIMEOUT:
            return Status::TIMEOUT;
        case WebcamDevice::Status::IO_ERROR:
            return Status::IO_ERROR;
        case WebcamDevice::Status::INTERNAL_ERROR:
            return Status::INTERNAL_ERROR;
        case WebcamDevice::Status::INVALID_ARGUMENT:
            return Status::INVALID_ARGUMENT;
        default:
            return Status::INTERNAL_ERROR;
    }
}

bool WebcamDeviceAdapter::isCapturing() const {
    return device_ && device_->isCapturing();
}

ImagingDeviceAdapter::Config WebcamDeviceAdapter::getCurrentConfiguration() const {
    return currentConfig_;
}

double WebcamDeviceAdapter::getCurrentFrameRate() const {
    return device_ ? device_->getCurrentFrameRate() : 0.0;
}

std::map<std::string, std::string> WebcamDeviceAdapter::getDiagnostics() const {
    return device_ ? device_->getDiagnostics() : std::map<std::string, std::string>{};
}

} // namespace medical::imaging