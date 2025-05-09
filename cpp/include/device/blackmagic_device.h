#pragma once

#include "device/ultrasound_device.h"
#include <mutex>
#include <atomic>
#include <thread>

// Forward declarations for Blackmagic SDK classes
class IDeckLink;
class IDeckLinkInput;
class IDeckLinkConfiguration;
class IDeckLinkDisplayMode;
class IDeckLinkVideoInputFrame;
class IDeckLinkAudioInputPacket;

namespace medical {
namespace imaging {

/**
 * @class BlackmagicDevice
 * @brief Implementation of UltrasoundDevice for Blackmagic capture devices
 * 
 * This class provides a concrete implementation of the UltrasoundDevice interface
 * specifically for Blackmagic capture devices, using the Blackmagic SDK.
 */
class BlackmagicDevice : public UltrasoundDevice {
public:
    /**
     * @brief Constructor
     * @param deckLink Pointer to the DeckLink device
     */
    explicit BlackmagicDevice(void* deckLink);
    
    /**
     * @brief Destructor
     */
    ~BlackmagicDevice() override;
    
    // UltrasoundDevice interface implementation
    std::string getDeviceId() const override;
    std::string getDeviceName() const override;
    std::string getDeviceModel() const override;
    Status initialize(const Config& config) override;
    Status startCapture(std::function<void(std::shared_ptr<Frame>)> frameCallback) override;
    Status stopCapture() override;
    bool isCapturing() const override;
    std::vector<Config> getSupportedConfigurations() const override;
    Config getCurrentConfiguration() const override;

private:
    // Inner callback class for DeckLink SDK
    class InputCallback;
    friend class InputCallback;
    
    // Convert between Blackmagic types and our types
    static uint32_t getBlackmagicPixelFormat(const std::string& format);
    static std::string getPixelFormatString(uint32_t blackmagicFormat);
    
    // Convert Blackmagic frames to our Frame type
    std::shared_ptr<Frame> convertFrame(IDeckLinkVideoInputFrame* videoFrame, 
                                       IDeckLinkAudioInputPacket* audioPacket);
    
    // Find a compatible display mode
    bool findMatchingDisplayMode(const Config& config, IDeckLinkDisplayMode** mode);
    
    // Member variables
    IDeckLink* deckLink_;
    IDeckLinkInput* deckLinkInput_;
    IDeckLinkConfiguration* deckLinkConfig_;
    std::shared_ptr<InputCallback> callback_;
    
    Config currentConfig_;
    std::function<void(std::shared_ptr<Frame>)> frameCallback_;
    
    std::atomic<bool> isCapturing_;
    mutable std::mutex mutex_;
    std::string deviceId_;
    std::string deviceName_;
    std::string deviceModel_;
};

} // namespace imaging
} // namespace medical