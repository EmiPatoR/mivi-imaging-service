#pragma once

#include "device/imaging_device_adapter.h"
#include "device/blackmagic_device.h"
#include "device/webcam_device.h"

namespace medical::imaging {

/**
 * @class BlackmagicDeviceAdapter
 * @brief Adapter for Blackmagic devices that implements the ImagingDeviceAdapter interface
 */
class BlackmagicDeviceAdapter : public ImagingDeviceAdapter {
public:
    /**
     * @brief Constructor
     * @param device Shared pointer to the Blackmagic device
     */
    explicit BlackmagicDeviceAdapter(std::shared_ptr<BlackmagicDevice> device);
    
    /**
     * @brief Destructor
     */
    ~BlackmagicDeviceAdapter() override = default;
    
    // ImagingDeviceAdapter interface implementation
    std::string getDeviceId() const override;
    std::string getDeviceName() const override;
    std::string getDeviceModel() const override;
    DeviceType getDeviceType() const override;
    Status initialize(const Config &config) override;
    Status startCapture(std::function<void(std::shared_ptr<Frame>)> frameCallback) override;
    Status stopCapture() override;
    bool isCapturing() const override;
    Config getCurrentConfiguration() const override;
    double getCurrentFrameRate() const override;
    std::map<std::string, std::string> getDiagnostics() const override;
    
private:
    std::shared_ptr<BlackmagicDevice> device_;
    Config currentConfig_;
    mutable std::mutex mutex_;
};

/**
 * @class WebcamDeviceAdapter
 * @brief Adapter for webcam devices that implements the ImagingDeviceAdapter interface
 */
class WebcamDeviceAdapter : public ImagingDeviceAdapter {
public:
    /**
     * @brief Constructor
     * @param device Shared pointer to the webcam device
     */
    explicit WebcamDeviceAdapter(std::shared_ptr<WebcamDevice> device);

    /**
     * @brief Destructor
     */
    ~WebcamDeviceAdapter() override = default;

    // ImagingDeviceAdapter interface implementation
    std::string getDeviceId() const override;
    std::string getDeviceName() const override;
    std::string getDeviceModel() const override;
    DeviceType getDeviceType() const override;
    Status initialize(const Config &config) override;
    Status startCapture(std::function<void(std::shared_ptr<Frame>)> frameCallback) override;
    Status stopCapture() override;
    bool isCapturing() const override;
    Config getCurrentConfiguration() const override;
    double getCurrentFrameRate() const override;
    std::map<std::string, std::string> getDiagnostics() const override;

private:
    std::shared_ptr<WebcamDevice> device_;
    Config currentConfig_;
    mutable std::mutex mutex_;
};

} // namespace medical::imaging