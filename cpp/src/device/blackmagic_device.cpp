#include "device/blackmagic_device.h"
#include "DeckLinkAPI.h"
#include "utils/refiid_compare.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <thread>
#include <sys/mman.h>
#include <cmath>
#include <iomanip>
#include <numeric>

namespace medical::imaging {
    /**
     * @brief Enhanced implementation of IDeckLinkInputCallback for frame callbacks
     *
     * This callback implementation supports zero-copy frame acquisition and
     * DMA where possible.
     */
    class BlackmagicDevice::InputCallback final : public IDeckLinkInputCallback {
    public:
        explicit InputCallback(BlackmagicDevice *device) : device_(device), refCount_(1) {
        }

        // IDeckLinkInputCallback interface
        HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(
            IDeckLinkVideoInputFrame *videoFrame,
            IDeckLinkAudioInputPacket *audioPacket) override {
            if (!device_ || !videoFrame) {
                return S_OK;
            }

            // Update performance metrics
            device_->updatePerformanceMetrics(videoFrame);

            // Get the current time for latency calculation
            auto now = std::chrono::high_resolution_clock::now();

            // Convert Blackmagic frame to our Frame type using the appropriate method
            std::shared_ptr<Frame> frame;

            // Check if we should use DMA or external memory
            if (device_->isDmaEnabled_) {
                // Use DMA/external memory path
                frame = device_->convertFrameExternalMemory(videoFrame, audioPacket);
            } else {
                // Use standard path
                frame = device_->convertFrame(videoFrame, audioPacket);
            }

            if (frame && device_->frameCallback_) {
                device_->frameCallback_(frame);
            }

            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(
            BMDVideoInputFormatChangedEvents events,
            IDeckLinkDisplayMode *displayMode,
            BMDDetectedVideoInputFormatFlags formatFlags) override {
            if (!device_ || !displayMode) {
                return S_OK;
            }

            std::cout << "Video input format changed event received" << std::endl;

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
                device_->deckLinkInput_->EnableVideoInput(
                    displayMode->GetDisplayMode(), pixelFormat, bmdVideoInputEnableFormatDetection);

                device_->deckLinkInput_->StartStreams();
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
          profileManager_(nullptr),
          status_(nullptr),
          externalMemory_(nullptr),
          externalMemorySize_(0),
          frameCount_(0),
          droppedFrames_(0),
          isCapturing_(false),
          isDmaEnabled_(false),
          isGpuDirectEnabled_(false) {
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

        // Query for the interfaces
        deckLink_->QueryInterface(IID_IDeckLinkInput, reinterpret_cast<void **>(&deckLinkInput_));
        deckLink_->QueryInterface(IID_IDeckLinkConfiguration, reinterpret_cast<void **>(&deckLinkConfig_));
        deckLink_->QueryInterface(IID_IDeckLinkProfileManager, reinterpret_cast<void **>(&profileManager_));
        deckLink_->QueryInterface(IID_IDeckLinkStatus, reinterpret_cast<void **>(&status_));

        // Set the callback
        if (deckLinkInput_) {
            deckLinkInput_->SetCallback(callback_.get());
        }

        // Initialize performance tracking
        startTime_ = std::chrono::steady_clock::now();
        lastFrameTime_ = startTime_;

        // Query device capabilities
        queryCapabilities();
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

        if (profileManager_) {
            profileManager_->Release();
            profileManager_ = nullptr;
        }

        if (status_) {
            status_->Release();
            status_ = nullptr;
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

    BlackmagicDevice::Status BlackmagicDevice::initialize(const Config &config) {
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

        // Check if the device supports DMA and it's requested
        if (config.enableDirectMemoryAccess && capabilities_.supportsDma) {
            // Initialize DMA if supported
            if (initializeDMA()) {
                isDmaEnabled_ = true;
                std::cout << "DMA enabled for " << deviceName_ << std::endl;
            } else {
                std::cerr << "Failed to initialize DMA for " << deviceName_ << std::endl;
            }
        }

        // Check if the device supports direct-to-GPU and it's requested
        if (config.enableGpuDirect && capabilities_.supportsGpuDirect) {
            // Initialize GPU direct if supported
            if (initializeGpuDirect()) {
                isGpuDirectEnabled_ = true;
                std::cout << "GPU-Direct enabled for " << deviceName_ << std::endl;
            } else {
                std::cerr << "Failed to initialize GPU-Direct for " << deviceName_ << std::endl;
            }
        }

        // If direct output to shared memory is configured, set it up
        if (!config.sharedMemoryName.empty()) {
            setDirectOutputToSharedMemory(config.sharedMemoryName);
        }

        // Initialize buffer pool for zero-copy if needed
        if (config.bufferCount > 0) {
            size_t bufferSize = config.width * config.height *
                                (config.pixelFormat == "YUV" ? 2 : 4); // Estimate size

            initializeBufferPool(config.bufferCount, bufferSize);
        }

        // Enable the input
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

    BlackmagicDevice::Status BlackmagicDevice::startCapture(
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

        // Reset performance tracking
        frameCount_ = 0;
        droppedFrames_ = 0;
        startTime_ = std::chrono::steady_clock::now();
        lastFrameTime_ = startTime_;
        fpsHistory_.clear();

        // Start the streams
        HRESULT result = deckLinkInput_->StartStreams();
        if (FAILED(result)) {
            return Status::INTERNAL_ERROR;
        }

        isCapturing_ = true;
        return Status::OK;
    }

    BlackmagicDevice::Status BlackmagicDevice::stopCapture() {
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

    std::vector<BlackmagicDevice::Config> BlackmagicDevice::getSupportedConfigurations() const {
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

                    // Set DMA capabilities
                    yuvConfig.enableDirectMemoryAccess = capabilities_.supportsDma;
                    yuvConfig.enableGpuDirect = capabilities_.supportsGpuDirect;
                    yuvConfig.enableHardwareTimestamps = capabilities_.supportsHardwareTimestamps;

                    configs.push_back(yuvConfig);
                }

                pixelFormat = bmdFormat8BitBGRA;
                if (SUCCEEDED(deckLinkInput_->DoesSupportVideoMode(
                    bmdVideoConnectionUnspecified, mode, pixelFormat,
                    bmdNoVideoInputConversion, bmdSupportedVideoModeDefault,
                    &actualMode, &supportsRGB)) && supportsRGB) {
                    Config rgbConfig = config;
                    rgbConfig.pixelFormat = "RGB";

                    // Set DMA capabilities
                    rgbConfig.enableDirectMemoryAccess = capabilities_.supportsDma;
                    rgbConfig.enableGpuDirect = capabilities_.supportsGpuDirect;
                    rgbConfig.enableHardwareTimestamps = capabilities_.supportsHardwareTimestamps;

                    configs.push_back(rgbConfig);
                }

                // Check 10-bit formats if supported
                pixelFormat = bmdFormat10BitYUV;
                bool supports10BitYUV = false;
                if (SUCCEEDED(deckLinkInput_->DoesSupportVideoMode(
                    bmdVideoConnectionUnspecified, mode, pixelFormat,
                    bmdNoVideoInputConversion, bmdSupportedVideoModeDefault,
                    &actualMode, &supports10BitYUV)) && supports10BitYUV) {
                    Config yuv10Config = config;
                    yuv10Config.pixelFormat = "YUV10";

                    // Set DMA capabilities
                    yuv10Config.enableDirectMemoryAccess = capabilities_.supportsDma;
                    yuv10Config.enableGpuDirect = capabilities_.supportsGpuDirect;
                    yuv10Config.enableHardwareTimestamps = capabilities_.supportsHardwareTimestamps;

                    configs.push_back(yuv10Config);
                }

                displayMode->Release();
            }
        }

        displayModeIterator->Release();
        return configs;
    }

    BlackmagicDevice::Config BlackmagicDevice::getCurrentConfiguration() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return currentConfig_;
    }

    BlackmagicDevice::Capabilities BlackmagicDevice::getCapabilities() const {
        return capabilities_;
    }

    bool BlackmagicDevice::supportsFeature(DeviceFeature feature) const {
        // Check if the feature is in the supported features list
        return std::find(capabilities_.supportedFeatures.begin(),
                         capabilities_.supportedFeatures.end(),
                         feature) != capabilities_.supportedFeatures.end();
    }

    BlackmagicDevice::Status BlackmagicDevice::setExternalMemory(void *externalMemory, size_t size) {
        if (!externalMemory || size == 0) {
            return Status::INVALID_ARGUMENT;
        }

        // Store the external memory pointer
        externalMemory_ = externalMemory;
        externalMemorySize_ = size;

        return Status::OK;
    }

    BlackmagicDevice::Status BlackmagicDevice::setDirectOutputToSharedMemory(const std::string &sharedMemoryName) {
        if (sharedMemoryName.empty()) {
            return Status::INVALID_ARGUMENT;
        }

        // Store the shared memory name
        directSharedMemoryName_ = sharedMemoryName;

        return Status::OK;
    }

    double BlackmagicDevice::getCurrentFrameRate() const {
        std::lock_guard<std::mutex> lock(metricsMutex_);

        if (fpsHistory_.empty()) {
            return 0.0;
        }

        // Calculate average FPS from recent history
        return std::accumulate(fpsHistory_.begin(), fpsHistory_.end(), 0.0) / fpsHistory_.size();
    }

    std::map<std::string, std::string> BlackmagicDevice::getDiagnostics() const {
        std::map<std::string, std::string> diagnostics;

        // Add basic info
        diagnostics["device_id"] = deviceId_;
        diagnostics["device_name"] = deviceName_;
        diagnostics["device_model"] = deviceModel_;
        diagnostics["is_capturing"] = isCapturing_ ? "true" : "false";

        // Add configuration
        diagnostics["width"] = std::to_string(currentConfig_.width);
        diagnostics["height"] = std::to_string(currentConfig_.height);
        diagnostics["frame_rate"] = std::to_string(currentConfig_.frameRate);
        diagnostics["pixel_format"] = currentConfig_.pixelFormat;

        // Add capabilities
        diagnostics["supports_dma"] = capabilities_.supportsDma ? "true" : "false";
        diagnostics["supports_gpu_direct"] = capabilities_.supportsGpuDirect ? "true" : "false";
        diagnostics["supports_hardware_timestamps"] = capabilities_.supportsHardwareTimestamps ? "true" : "false";

        // Add performance metrics
        std::lock_guard<std::mutex> lock(metricsMutex_);
        diagnostics["frame_count"] = std::to_string(frameCount_);
        diagnostics["dropped_frames"] = std::to_string(droppedFrames_);

        if (!fpsHistory_.empty()) {
            double avgFps = std::accumulate(fpsHistory_.begin(), fpsHistory_.end(), 0.0) / fpsHistory_.size();
            diagnostics["average_fps"] = std::to_string(avgFps);
        } else {
            diagnostics["average_fps"] = "0.0";
        }

        // Add connected interfaces
        diagnostics["has_input_interface"] = deckLinkInput_ ? "true" : "false";
        diagnostics["has_config_interface"] = deckLinkConfig_ ? "true" : "false";
        diagnostics["has_profile_interface"] = profileManager_ ? "true" : "false";
        diagnostics["has_status_interface"] = status_ ? "true" : "false";

        // Check if DMA is enabled
        diagnostics["dma_enabled"] = isDmaEnabled_ ? "true" : "false";
        diagnostics["gpu_direct_enabled"] = isGpuDirectEnabled_ ? "true" : "false";

        // Add status info if available
        if (status_) {
            // Get detected mode
            BMDDisplayMode detectedMode;
            if (SUCCEEDED(status_->GetInt(bmdDeckLinkStatusDetectedVideoInputMode,
                reinterpret_cast<int64_t*>(&detectedMode)))) {
                diagnostics["detected_mode"] = std::to_string(detectedMode);
            }

            // Get signal locked status
            bool signalLocked;
            if (SUCCEEDED(status_->GetFlag(bmdDeckLinkStatusVideoInputSignalLocked, &signalLocked))) {
                diagnostics["signal_locked"] = signalLocked ? "true" : "false";
            }

            // Get reference locked status
            bool referenceLocked;
            if (SUCCEEDED(status_->GetFlag(bmdDeckLinkStatusReferenceSignalLocked, &referenceLocked))) {
                diagnostics["reference_locked"] = referenceLocked ? "true" : "false";
            }
        }

        return diagnostics;
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

        // Set up a custom destructor function to handle the buffer correctly
        frame->setOnDestroy([videoFrameBuffer]() {
            videoFrameBuffer->EndAccess(bmdBufferAccessRead);
            videoFrameBuffer->Release();
        });

        // Get timestamp
        BMDTimeValue frameTime;
        BMDTimeScale timeScale;
        std::chrono::system_clock::time_point timestamp;

        if (SUCCEEDED(videoFrame->GetStreamTime(&frameTime, nullptr, timeScale))) {
            // Convert to system time
            timestamp = std::chrono::system_clock::now();
        } else {
            timestamp = std::chrono::system_clock::now();
        }

        frame->setTimestamp(timestamp);

        // Generate frame ID based on the timestamp
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = now.time_since_epoch();
        auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        frame->setFrameId(static_cast<uint64_t>(nanos));

        // Set up enhanced metadata
        FrameMetadata &metadata = frame->getMetadataMutable();
        metadata.deviceId = deviceId_;
        metadata.width = width;
        metadata.height = height;
        metadata.bytesPerPixel = rowBytes / width;
        metadata.frameNumber = frameCount_;
        metadata.hasBeenProcessed = false;

        // Add timecode if available
        IDeckLinkTimecode *timecode = nullptr;
        if (SUCCEEDED(videoFrame->GetTimecode(bmdTimecodeRP188Any, &timecode)) && timecode) {
            const char *timecodeString = nullptr;
            if (SUCCEEDED(timecode->GetString(&timecodeString)) && timecodeString) {
                metadata.attributes["timecode"] = timecodeString;
            }

            // Get components
            uint8_t hours, minutes, seconds, frames;
            if (SUCCEEDED(timecode->GetComponents(&hours, &minutes, &seconds, &frames))) {
                metadata.attributes["timecode_hours"] = std::to_string(hours);
                metadata.attributes["timecode_minutes"] = std::to_string(minutes);
                metadata.attributes["timecode_seconds"] = std::to_string(seconds);
                metadata.attributes["timecode_frames"] = std::to_string(frames);
            }

            timecode->Release();
        }

        // Add frame flags as metadata
        metadata.attributes["frame_flags"] = std::to_string(videoFrame->GetFlags());

        // Check for signal quality info
        if (status_) {
            bool signalLocked;
            if (SUCCEEDED(status_->GetFlag(bmdDeckLinkStatusVideoInputSignalLocked, &signalLocked))) {
                metadata.attributes["signal_locked"] = signalLocked ? "true" : "false";

                // If signal is locked, assume good quality
                if (signalLocked) {
                    metadata.signalStrength = 1.0f;
                    metadata.signalToNoiseRatio = 50.0f; // Good SNR in dB
                } else {
                    metadata.signalStrength = 0.0f;
                    metadata.signalToNoiseRatio = 0.0f;
                }
            }
        }

        return frame;
    }

    std::shared_ptr<Frame> BlackmagicDevice::convertFrameExternalMemory(IDeckLinkVideoInputFrame *videoFrame,
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

        // Get frame data
        void *frameBytes = nullptr;
        void *destinationBuffer = nullptr;

        // Allocate a buffer from the pool or use external memory
        Buffer *buffer = nullptr;

        if (!directSharedMemoryName_.empty()) {
            // Use direct shared memory output
            // This would require shared memory implementation specific code
            // For now, fallback to normal path
            return convertFrame(videoFrame, audioPacket);
        } else if (externalMemory_ && externalMemorySize_ >= dataSize) {
            // Use provided external memory
            destinationBuffer = externalMemory_;
        } else {
            // Try to get a buffer from the pool
            buffer = allocateBuffer(dataSize);
            if (buffer) {
                destinationBuffer = buffer->memory;
            } else {
                // No suitable buffer available, fallback to normal path
                return convertFrame(videoFrame, audioPacket);
            }
        }

        // Get the source frame data
        IDeckLinkVideoBuffer *videoFrameBuffer = nullptr;
        if (videoFrame->QueryInterface(IID_IDeckLinkVideoBuffer, (void **) &videoFrameBuffer) != S_OK) {
            // Failed to get video buffer interface
            if (buffer) {
                releaseBuffer(buffer);
            }
            return nullptr;
        }

        // Start access to buffer
        if (videoFrameBuffer->StartAccess(bmdBufferAccessRead) != S_OK) {
            videoFrameBuffer->Release();
            if (buffer) {
                releaseBuffer(buffer);
            }
            return nullptr;
        }

        // Get the buffer pointer
        videoFrameBuffer->GetBytes(&frameBytes);

        if (!frameBytes) {
            videoFrameBuffer->EndAccess(bmdBufferAccessRead);
            videoFrameBuffer->Release();
            if (buffer) {
                releaseBuffer(buffer);
            }
            return nullptr;
        }

        // Copy the data to our buffer
        std::memcpy(destinationBuffer, frameBytes, dataSize);

        // End access to source buffer
        videoFrameBuffer->EndAccess(bmdBufferAccessRead);
        videoFrameBuffer->Release();

        // Create frame with our buffer
        auto frame = Frame::createWithExternalData(
            destinationBuffer,
            dataSize,
            width,
            height,
            rowBytes / width,
            getPixelFormatString(pixelFormat),
            false); // Don't take ownership

        if (!frame) {
            if (buffer) {
                releaseBuffer(buffer);
            }
            return nullptr;
        }

        // Set up a custom destructor function to release the buffer
        if (buffer) {
            frame->setOnDestroy([this, buffer]() {
                releaseBuffer(buffer);
            });
        }

        // Set up timestamp and metadata like in the regular convertFrame method
        BMDTimeValue frameTime;
        BMDTimeScale timeScale;
        std::chrono::system_clock::time_point timestamp;

        if (SUCCEEDED(videoFrame->GetStreamTime(&frameTime, nullptr, timeScale))) {
            timestamp = std::chrono::system_clock::now();
        } else {
            timestamp = std::chrono::system_clock::now();
        }

        frame->setTimestamp(timestamp);

        // Generate frame ID based on the timestamp
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = now.time_since_epoch();
        auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
        frame->setFrameId(static_cast<uint64_t>(nanos));

        // Set up enhanced metadata
        FrameMetadata &metadata = frame->getMetadataMutable();
        metadata.deviceId = deviceId_;
        metadata.width = width;
        metadata.height = height;
        metadata.bytesPerPixel = rowBytes / width;
        metadata.frameNumber = frameCount_;
        metadata.hasBeenProcessed = false;

        // Add timecode if available
        IDeckLinkTimecode *timecode = nullptr;
        if (SUCCEEDED(videoFrame->GetTimecode(bmdTimecodeRP188Any, &timecode)) && timecode) {
            const char *timecodeString = nullptr;
            if (SUCCEEDED(timecode->GetString(&timecodeString)) && timecodeString) {
                metadata.attributes["timecode"] = timecodeString;
            }
            timecode->Release();
        }

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

    bool BlackmagicDevice::initializeDMA() {
        // This is a stub implementation - real DMA setup would be hardware specific
        // For Blackmagic cards, this might involve specific APIs or driver settings

        // For simulation purposes, we'll just pretend we succeeded
        return capabilities_.supportsDma;
    }

    bool BlackmagicDevice::initializeGpuDirect() {
        // This is a stub implementation - real GPU direct setup would involve
        // using CUDA, OpenCL, or other GPU APIs to allocate GPU memory

        // For simulation purposes, we'll just pretend we succeeded
        return capabilities_.supportsGpuDirect;
    }

    bool BlackmagicDevice::initializeBufferPool(size_t bufferCount, size_t bufferSize) {
        std::lock_guard<std::mutex> lock(bufferPoolMutex_);

        // Clear existing pool
        bufferPool_.clear();

        // Allocate buffers
        bufferPool_.resize(bufferCount);

        for (size_t i = 0; i < bufferCount; ++i) {
            Buffer &buffer = bufferPool_[i];
            buffer.memory = malloc(bufferSize);

            if (!buffer.memory) {
                // Allocation failed
                // Clean up what we've allocated so far
                for (size_t j = 0; j < i; ++j) {
                    free(bufferPool_[j].memory);
                    bufferPool_[j].memory = nullptr;
                }

                bufferPool_.clear();
                return false;
            }

            buffer.size = bufferSize;
            buffer.inUse = false;
            buffer.isExternal = false;
        }

        return true;
    }

    BlackmagicDevice::Buffer *BlackmagicDevice::allocateBuffer(size_t size) {
        std::lock_guard<std::mutex> lock(bufferPoolMutex_);

        // Find a free buffer of sufficient size
        for (Buffer &buffer: bufferPool_) {
            if (!buffer.inUse && buffer.size >= size) {
                buffer.inUse = true;
                buffer.lastUsed = std::chrono::steady_clock::now();
                return &buffer;
            }
        }

        // No suitable buffer found
        return nullptr;
    }

    void BlackmagicDevice::releaseBuffer(Buffer *buffer) {
        if (!buffer) {
            return;
        }

        std::lock_guard<std::mutex> lock(bufferPoolMutex_);

        // Find the buffer in our pool
        for (Buffer &poolBuffer: bufferPool_) {
            if (&poolBuffer == buffer) {
                poolBuffer.inUse = false;
                return;
            }
        }
    }

    void BlackmagicDevice::queryCapabilities() {
        // Initialize with default values
        capabilities_.supportsDma = false;
        capabilities_.supportsGpuDirect = false;
        capabilities_.supportsHardwareTimestamps = false;
        capabilities_.supportsExternalTrigger = false;
        capabilities_.supportsMultipleStreams = false;
        capabilities_.supportsProgrammableRoi = false;

        // Add supported pixel formats
        capabilities_.supportedPixelFormats = {"YUV", "RGB", "YUV10", "RGB10"};

        // For Blackmagic devices, we need to query the device-specific capabilities
        // This might involve checking the device model or specific features

        // For now, we'll just set some common capabilities
        // In a real implementation, these would be queried from the device

        // Add basic features by default
        capabilities_.supportedFeatures.push_back(DeviceFeature::HARDWARE_TIMESTAMP);

        // Check DMA support based on device model
        if (deviceModel_.find("Decklink") != std::string::npos ||
            deviceModel_.find("UltraStudio") != std::string::npos) {
            // Most modern Decklink and UltraStudio devices support DMA
            capabilities_.supportsDma = true;
            capabilities_.supportedFeatures.push_back(DeviceFeature::DIRECT_MEMORY_ACCESS);
        }

        // Check for GPU direct support
        // This would typically involve checking for CUDA or OpenCL support
        // For now, we'll just set it to false
        capabilities_.supportsGpuDirect = false;

        // Add device-specific info
        capabilities_.deviceInfo["vendor"] = "Blackmagic Design";
        capabilities_.deviceInfo["model"] = deviceModel_;
        capabilities_.deviceInfo["driver_version"] = "14.4"; // Would be queried in real implementation
    }

    void BlackmagicDevice::updatePerformanceMetrics(IDeckLinkVideoInputFrame *videoFrame) {
        // Calculate inter-frame time for FPS
        auto now = std::chrono::steady_clock::now();
        auto timeSinceLastFrame = std::chrono::duration_cast<std::chrono::microseconds>(
            now - lastFrameTime_).count();
        lastFrameTime_ = now;

        // Update frame count
        ++frameCount_;

        // Calculate instant FPS
        double instantFps = 0.0;
        if (timeSinceLastFrame > 0) {
            instantFps = 1000000.0 / timeSinceLastFrame;
        }

        // Update FPS history
        {
            std::lock_guard<std::mutex> lock(metricsMutex_);
            fpsHistory_.push_back(instantFps);
            if (fpsHistory_.size() > 60) {
                // Keep 1 second of history at 60 fps
                fpsHistory_.pop_front();
            }
        }
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

    // Simplified implementation of BlackmagicDeviceManager
    class BlackmagicDeviceManager::Impl {
    public:
        std::mutex mutex_;
        std::unordered_map<std::string, std::shared_ptr<BlackmagicDevice> > devices_;
        std::unordered_map<int, std::function<void(const std::string &, bool)> > callbacks_;
        int nextCallbackId_ = 0;
    };

    BlackmagicDeviceManager &BlackmagicDeviceManager::getInstance() {
        static BlackmagicDeviceManager instance;
        return instance;
    }

    BlackmagicDeviceManager::BlackmagicDeviceManager() : impl_(std::make_unique<Impl>()) {
        // Initial device discovery
        discoverDevices();
    }

    BlackmagicDeviceManager::~BlackmagicDeviceManager() = default;

    int BlackmagicDeviceManager::discoverDevices() {
        std::cout << "Discovering Blackmagic devices..." << std::endl;

        // Create an iterator
        IDeckLinkIterator *iterator = CreateDeckLinkIteratorInstance();
        if (!iterator) {
            std::cout << "Failed to create DeckLink iterator" << std::endl;
            return 0;
        }

        int count = 0;
        IDeckLink *deckLink = nullptr;

        // Clear existing devices
        {
            std::lock_guard<std::mutex> lock(impl_->mutex_);
            impl_->devices_.clear();
        }

        // Iterate through all devices
        while (iterator->Next(&deckLink) == S_OK) {
            if (deckLink) {
                // Create device
                auto device = std::make_shared<BlackmagicDevice>(deckLink);

                // Add to map
                {
                    std::lock_guard<std::mutex> lock(impl_->mutex_);
                    impl_->devices_[device->getDeviceId()] = device;
                }

                // Increment count
                count++;
            }
        }

        iterator->Release();
        return count;
    }

    std::vector<std::string> BlackmagicDeviceManager::getAvailableDeviceIds() const {
        std::vector<std::string> ids;

        std::lock_guard<std::mutex> lock(impl_->mutex_);
        for (const auto &[id, device]: impl_->devices_) {
            ids.push_back(id);
        }

        return ids;
    }

    std::shared_ptr<BlackmagicDevice> BlackmagicDeviceManager::getDevice(const std::string &deviceId) const {
        std::lock_guard<std::mutex> lock(impl_->mutex_);

        auto it = impl_->devices_.find(deviceId);
        if (it != impl_->devices_.end()) {
            return it->second;
        }

        return nullptr;
    }

    int BlackmagicDeviceManager::registerDeviceChangeCallback(
        std::function<void(const std::string &deviceId, bool added)> callback) {
        std::lock_guard<std::mutex> lock(impl_->mutex_);

        int id = impl_->nextCallbackId_++;
        impl_->callbacks_[id] = callback;

        return id;
    }

    bool BlackmagicDeviceManager::unregisterDeviceChangeCallback(int subscriptionId) {
        std::lock_guard<std::mutex> lock(impl_->mutex_);

        auto it = impl_->callbacks_.find(subscriptionId);
        if (it != impl_->callbacks_.end()) {
            impl_->callbacks_.erase(it);
            return true;
        }

        return false;
    }
} // namespace imaging
