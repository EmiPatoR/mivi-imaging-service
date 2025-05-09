#include "device/blackmagic_device.h"
#include "DeckLinkAPI.h"
#include "utils/refiid_compare.h"
#include "utils/video_constants.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <chrono>


namespace medical::imaging {
    /**
     * @brief Implementation of IDeckLinkInputCallback for frame callbacks
     */
    class BlackmagicDevice::InputCallback final : public IDeckLinkInputCallback {
    public:
        explicit InputCallback(BlackmagicDevice *device) : device_(device), refCount_(1) {
        }

        // IDeckLinkInputCallback interface
        HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(
            IDeckLinkVideoInputFrame *videoFrame,
            IDeckLinkAudioInputPacket *audioPacket) override {
            if (!device_) {
                return S_OK;
            }

            // Convert Blackmagic frame to our Frame type
            auto frame = device_->convertFrame(videoFrame, audioPacket);
            if (frame && device_->frameCallback_) {
                device_->frameCallback_(frame);
            }

            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(
            BMDVideoInputFormatChangedEvents events,
            IDeckLinkDisplayMode *displayMode,
            const BMDDetectedVideoInputFormatFlags formatFlags) override {
            // Handle format changes if needed
            if (!device_ || !displayMode) {
                return S_OK;
            }

            // Get the new format details
            const long width = displayMode->GetWidth();
            const long height = displayMode->GetHeight();
            BMDTimeValue frameDuration;
            BMDTimeScale timeScale;
            displayMode->GetFrameRate(&frameDuration, &timeScale);

            const double frameRate = static_cast<double>(timeScale) / frameDuration;
            const BMDPixelFormat pixelFormat = formatFlags & bmdDetectedVideoInputRGB444
                                             ? bmdFormat8BitBGRA
                                             : bmdFormat8BitYUV;

            // Update the current configuration
            std::unique_lock<std::mutex> lock(device_->mutex_);
            device_->currentConfig_.width = width;
            device_->currentConfig_.height = height;
            device_->currentConfig_.frameRate = frameRate;
            device_->currentConfig_.pixelFormat = device_->getPixelFormatString(pixelFormat);

            // If we're capturing, we need to stop and restart with the new format
            if (device_->isCapturing_) {
                device_->deckLinkInput_->StopStreams();

                // Enable the new input format
                HRESULT result = device_->deckLinkInput_->EnableVideoInput(
                    displayMode->GetDisplayMode(), pixelFormat, bmdVideoInputEnableFormatDetection);

                if (SUCCEEDED(result)) {
                    device_->deckLinkInput_->StartStreams();
                }
            }

            return S_OK;
        }

        // IUnknown interface
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID *ppv) override {
            if (!ppv) {
                return E_POINTER;
            }

            if (iid == IID_IUnknown || iid == IID_IDeckLinkInputCallback) {
                *ppv = static_cast<IDeckLinkInputCallback *>(this);
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
        BlackmagicDevice *device_;
        std::atomic<ULONG> refCount_;
    };

    BlackmagicDevice::BlackmagicDevice(void *deckLink)
        : deckLink_(static_cast<IDeckLink *>(deckLink)),
          deckLinkInput_(nullptr),
          deckLinkConfig_(nullptr),
          isCapturing_(false) {
        // Get the device name and ID
        const char *name = nullptr;
        if (SUCCEEDED(deckLink_->GetDisplayName(&name))) {
            deviceName_ = name;
        } else {
            deviceName_ = "Unknown Device";
        }

        const char *model = nullptr;
        if (SUCCEEDED(deckLink_->GetModelName(&model))) {
            deviceModel_ = model;
        } else {
            deviceModel_ = "Unknown Model";
        }

        // Generate a unique ID
        std::stringstream ss;
        ss << "blackmagic_" << reinterpret_cast<uintptr_t>(deckLink_);
        deviceId_ = ss.str();

        // Create the callback
        callback_ = std::make_shared<InputCallback>(this);

        // Query for the input interface
        if (SUCCEEDED(deckLink_->QueryInterface(IID_IDeckLinkInput, reinterpret_cast<void**>(&deckLinkInput_)))) {
            // Set the callback
            deckLinkInput_->SetCallback(callback_.get());
        }

        // Query for the configuration interface
        deckLink_->QueryInterface(IID_IDeckLinkConfiguration, reinterpret_cast<void **>(&deckLinkConfig_));
    }

    BlackmagicDevice::~BlackmagicDevice() {
        // Stop capturing if necessary
        if (isCapturing_) {
            stopCapture();
        }

        // Release interfaces
        if (deckLinkInput_) {
            deckLinkInput_->SetCallback(nullptr);
            deckLinkInput_->Release();
            deckLinkInput_ = nullptr;
        }

        if (deckLinkConfig_) {
            deckLinkConfig_->Release();
            deckLinkConfig_ = nullptr;
        }

        if (deckLink_) {
            deckLink_->Release();
            deckLink_ = nullptr;
        }
    }

    std::string BlackmagicDevice::getDeviceId() const {
        return deviceId_;
    }

    std::string BlackmagicDevice::getDeviceName() const {
        return deviceName_;
    }

    std::string BlackmagicDevice::getDeviceModel() const {
        return deviceModel_;
    }

    UltrasoundDevice::Status BlackmagicDevice::initialize(const Config &config) {
        std::unique_lock<std::mutex> lock(mutex_);

        if (!deckLinkInput_) {
            return Status::INIT_FAILED;
        }

        // Stop capture if it's running
        if (isCapturing_) {
            deckLinkInput_->StopStreams();
            isCapturing_ = false;
        }

        // Find a matching display mode
        IDeckLinkDisplayMode *displayMode = nullptr;
        if (!findMatchingDisplayMode(config, &displayMode)) {
            return Status::CONFIGURATION_ERROR;
        }

        // Get the pixel format
        uint32_t pixelFormat = getBlackmagicPixelFormat(config.pixelFormat);

        // Enable the input
        BMDVideoInputFlags flags = bmdVideoInputEnableFormatDetection;
        HRESULT result = deckLinkInput_->EnableVideoInput(
            displayMode->GetDisplayMode(), pixelFormat, flags);

        displayMode->Release();

        if (FAILED(result)) {
            return Status::INIT_FAILED;
        }

        // Enable audio if requested
        if (config.enableAudio) {
            result = deckLinkInput_->EnableAudioInput(
                bmdAudioSampleRate48kHz, bmdAudioSampleType16bitInteger, 2);

            if (FAILED(result)) {
                deckLinkInput_->DisableVideoInput();
                return Status::INIT_FAILED;
            }
        }

        // Store the configuration
        currentConfig_ = config;

        return Status::OK;
    }

    UltrasoundDevice::Status BlackmagicDevice::startCapture(
        std::function<void(std::shared_ptr<Frame>)> frameCallback) {
        std::unique_lock<std::mutex> lock(mutex_);

        if (!deckLinkInput_) {
            return Status::INIT_FAILED;
        }

        if (isCapturing_) {
            return Status::ALREADY_STREAMING;
        }

        // Store the callback
        frameCallback_ = frameCallback;

        // Start the streams
        HRESULT result = deckLinkInput_->StartStreams();
        if (FAILED(result)) {
            return Status::INTERNAL_ERROR;
        }

        isCapturing_ = true;
        return Status::OK;
    }

    UltrasoundDevice::Status BlackmagicDevice::stopCapture() {
        std::unique_lock<std::mutex> lock(mutex_);

        if (!deckLinkInput_) {
            return Status::INIT_FAILED;
        }

        if (!isCapturing_) {
            return Status::NOT_STREAMING;
        }

        // Stop the streams
        HRESULT result = deckLinkInput_->StopStreams();
        if (FAILED(result)) {
            return Status::INTERNAL_ERROR;
        }

        isCapturing_ = false;
        return Status::OK;
    }

    bool BlackmagicDevice::isCapturing() const {
        return isCapturing_;
    }

    std::vector<UltrasoundDevice::Config> BlackmagicDevice::getSupportedConfigurations() const {
        std::vector<Config> configs;

        if (!deckLinkInput_) {
            return configs;
        }

        // Get the display mode iterator
        IDeckLinkDisplayModeIterator *displayModeIterator = nullptr;
        HRESULT result = deckLinkInput_->GetDisplayModeIterator(&displayModeIterator);
        if (FAILED(result) || !displayModeIterator) {
            return configs;
        }

        // Iterate through the display modes
        IDeckLinkDisplayMode *displayMode = nullptr;
        while (displayModeIterator->Next(&displayMode) == S_OK) {
            if (displayMode) {
                Config config;

                // Get the mode details
                config.width = displayMode->GetWidth();
                config.height = displayMode->GetHeight();

                BMDTimeValue frameDuration;
                BMDTimeScale timeScale;
                displayMode->GetFrameRate(&frameDuration, &timeScale);
                config.frameRate = static_cast<double>(timeScale) / frameDuration;

                // Check support for different pixel formats
                bool supportsYUV = false;
                bool supportsRGB = false;

                BMDDisplayMode mode = displayMode->GetDisplayMode();
                BMDPixelFormat pixelFormat = bmdFormat8BitYUV;
                BMDDisplayMode actualMode;

                if (SUCCEEDED(deckLinkInput_->DoesSupportVideoMode(
                    bmdVideoConnectionUnspecified, mode, pixelFormat,
                    bmdNoVideoInputConversion, bmdSupportedVideoModeDefault,
                    &actualMode, &supportsYUV)) && supportsYUV) {
                    Config yuvConfig = config;
                    yuvConfig.pixelFormat = "YUV";
                    configs.push_back(yuvConfig);
                }

                pixelFormat = bmdFormat8BitBGRA;
                if (SUCCEEDED(deckLinkInput_->DoesSupportVideoMode(
                    bmdVideoConnectionUnspecified, mode, pixelFormat,
                    bmdNoVideoInputConversion, bmdSupportedVideoModeDefault,
                    &actualMode, &supportsRGB)) && supportsRGB) {
                    Config rgbConfig = config;
                    rgbConfig.pixelFormat = "RGB";
                    configs.push_back(rgbConfig);
                }

                displayMode->Release();
            }
        }

        displayModeIterator->Release();
        return configs;
    }

    UltrasoundDevice::Config BlackmagicDevice::getCurrentConfiguration() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return currentConfig_;
    }

    std::shared_ptr<Frame> BlackmagicDevice::convertFrame(IDeckLinkVideoInputFrame *videoFrame,
                                                          IDeckLinkAudioInputPacket *audioPacket) {
        if (!videoFrame) {
            return nullptr;
        }

        // Get frame details
        long width = videoFrame->GetWidth();
        long height = videoFrame->GetHeight();
        long rowBytes = videoFrame->GetRowBytes();
        BMDPixelFormat pixelFormat = videoFrame->GetPixelFormat();
        size_t dataSize = height * rowBytes;

        // Get frame data using IDeckLinkVideoBuffer interface
        void *frameBytes = nullptr;
        IDeckLinkVideoBuffer *videoFrameBuffer = nullptr;

        if (videoFrame->QueryInterface(IID_IDeckLinkVideoBuffer, (void **) &videoFrameBuffer) != S_OK) {
            // Failed to get video buffer interface
            return nullptr;
        }

        // Start access to buffer
        if (videoFrameBuffer->StartAccess(bmdBufferAccessRead) != S_OK) {
            videoFrameBuffer->Release();
            return nullptr;
        }

        // Get the buffer pointer
        videoFrameBuffer->GetBytes(&frameBytes);

        if (!frameBytes) {
            videoFrameBuffer->EndAccess(bmdBufferAccessRead);
            videoFrameBuffer->Release();
            return nullptr;
        }

        // Create a frame with external data (reference the BlackMagic buffer)
        auto frame = Frame::createWithExternalData(
            frameBytes,
            dataSize,
            width,
            height,
            rowBytes / width,
            getPixelFormatString(pixelFormat),
            false); // Don't take ownership - let DeckLink manage the buffer

        if (!frame) {
            videoFrameBuffer->EndAccess(bmdBufferAccessRead);
            videoFrameBuffer->Release();
            return nullptr;
        }

        // Set timestamp
        BMDTimeValue frameTime;
        BMDTimeScale timeScale;
        if (SUCCEEDED(videoFrame->GetStreamTime(&frameTime, nullptr, timeScale))) {
            auto timestamp = std::chrono::system_clock::now();
            frame->setTimestamp(timestamp);
        }

        // Set frame ID based on the timestamp
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = now.time_since_epoch();
        auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        frame->setFrameId(static_cast<uint64_t>(nanos));

        // Add metadata like timecode if available
        IDeckLinkTimecode *timecode = nullptr;
        if (SUCCEEDED(videoFrame->GetTimecode(bmdTimecodeRP188Any, &timecode)) && timecode) {
            const char *timecodeString = nullptr;
            if (SUCCEEDED(timecode->GetString(&timecodeString)) && timecodeString) {
                frame->setMetadata("timecode", timecodeString);
            }
            timecode->Release();
        }

        // Add frame flags as metadata
        std::stringstream flagsStr;
        flagsStr << videoFrame->GetFlags();
        frame->setMetadata("frame_flags", flagsStr.str());

        // Set up a custom destructor function to handle the buffer correctly
        frame->setOnDestroy([videoFrameBuffer]() {
            videoFrameBuffer->EndAccess(bmdBufferAccessRead);
            videoFrameBuffer->Release();
        });

        return frame;
    }

    bool BlackmagicDevice::findMatchingDisplayMode(const Config &config, IDeckLinkDisplayMode **mode) {
        if (!deckLinkInput_ || !mode) {
            return false;
        }

        *mode = nullptr;

        // Get the display mode iterator
        IDeckLinkDisplayModeIterator *displayModeIterator = nullptr;
        HRESULT result = deckLinkInput_->GetDisplayModeIterator(&displayModeIterator);
        if (FAILED(result) || !displayModeIterator) {
            return false;
        }

        // Variables to track the best match
        IDeckLinkDisplayMode *bestMatch = nullptr;
        int bestMatchScore = -1;

        // Iterate through the display modes
        IDeckLinkDisplayMode *displayMode = nullptr;
        while (displayModeIterator->Next(&displayMode) == S_OK) {
            if (displayMode) {
                // Get the mode details
                long width = displayMode->GetWidth();
                long height = displayMode->GetHeight();

                BMDTimeValue frameDuration;
                BMDTimeScale timeScale;
                displayMode->GetFrameRate(&frameDuration, &timeScale);
                double frameRate = static_cast<double>(timeScale) / frameDuration;

                // Check if this mode is supported with the requested pixel format
                bool supported = false;
                uint32_t pixelFormat = getBlackmagicPixelFormat(config.pixelFormat);
                BMDDisplayMode actualMode;

                result = deckLinkInput_->DoesSupportVideoMode(
                    bmdVideoConnectionUnspecified, displayMode->GetDisplayMode(), pixelFormat,
                    bmdNoVideoInputConversion, bmdSupportedVideoModeDefault, &actualMode, &supported);

                if (SUCCEEDED(result) && supported) {
                    // Calculate a match score (higher is better)
                    int widthDiff = std::abs(width - config.width);
                    int heightDiff = std::abs(height - config.height);
                    double frameRateDiff = std::abs(frameRate - config.frameRate);

                    // Normalize the differences
                    double widthScore = 1.0 - (static_cast<double>(widthDiff) / config.width);
                    double heightScore = 1.0 - (static_cast<double>(heightDiff) / config.height);
                    double frameRateScore = 1.0 - (frameRateDiff / config.frameRate);

                    // Calculate total score (weighted)
                    int score = static_cast<int>(
                        (widthScore * 0.4 + heightScore * 0.4 + frameRateScore * 0.2) * 100);

                    // If this is a perfect match, use it immediately
                    if (width == config.width && height == config.height &&
                        std::abs(frameRate - config.frameRate) < 0.1) {
                        if (bestMatch) {
                            bestMatch->Release();
                        }

                        *mode = displayMode; // Transfer ownership
                        displayModeIterator->Release();
                        return true;
                    }

                    // Otherwise, keep track of the best match
                    if (score > bestMatchScore) {
                        if (bestMatch) {
                            bestMatch->Release();
                        }

                        bestMatch = displayMode;
                        bestMatch->AddRef(); // Add a reference since we're storing it
                        bestMatchScore = score;
                    }
                }

                displayMode->Release();
            }
        }

        displayModeIterator->Release();

        // Use the best match if found
        if (bestMatch) {
            *mode = bestMatch; // Transfer ownership
            return true;
        }

        return false;
    }

    uint32_t BlackmagicDevice::getBlackmagicPixelFormat(const std::string &format) {
        if (format == "YUV" || format == "YUV422") {
            return bmdFormat8BitYUV;
        } else if (format == "YUV10" || format == "YUV422_10") {
            return bmdFormat10BitYUV;
        } else if (format == "RGB" || format == "BGRA") {
            return bmdFormat8BitBGRA;
        } else if (format == "RGB10") {
            return bmdFormat10BitRGB;
        } else {
            // Default to 8-bit YUV
            return bmdFormat8BitYUV;
        }
    }

    std::string BlackmagicDevice::getPixelFormatString(uint32_t blackmagicFormat) {
        switch (blackmagicFormat) {
            case bmdFormat8BitYUV:
                return "YUV";
            case bmdFormat10BitYUV:
                return "YUV10";
            case bmdFormat8BitBGRA:
                return "BGRA";
            case bmdFormat10BitRGB:
                return "RGB10";
            case bmdFormat12BitRGB:
                return "RGB12";
            default:
                return "Unknown";
        }
    }
} // namespace medical::imaging
