#include "api/imaging_service.h"
#include <chrono>
#include <thread>


namespace medical::imaging {
    ImagingService::ImagingService()
        : isInitialized_(false),
          isRunning_(false),
          stopRequested_(false),
          frameBufferHead_(0),
          frameBufferTail_(0),
          frameCount_(0),
          droppedFrames_(0) {
    }

    ImagingService::~ImagingService() {
        // Stop if running
        if (isRunning_) {
            stop();
        }
    }

    ImagingService::Status ImagingService::initialize(const Config &config) {
        // Check if already initialized
        if (isInitialized_) {
            return Status::ALREADY_RUNNING;
        }

        config_ = config;

        // Get the device
        auto &deviceManager = DeviceManager::getInstance();

        if (config.deviceId.empty()) {
            // Auto-select the first available device
            auto deviceIds = deviceManager.getAvailableDeviceIds();
            if (deviceIds.empty()) {
                return Status::DEVICE_ERROR;
            }

            device_ = deviceManager.getDevice(deviceIds[0]);
        } else {
            // Use the specified device
            device_ = deviceManager.getDevice(config.deviceId);
        }

        if (!device_) {
            return Status::DEVICE_ERROR;
        }

        // Initialize the device
        if (const auto deviceStatus = device_->initialize(config.deviceConfig); deviceStatus != UltrasoundDevice::Status::OK) {
            return Status::DEVICE_ERROR;
        }

        // Create frame processor
        FrameProcessor::Config processorConfig;
        processorConfig.enableSegmentation = config.enableSegmentation;
        processorConfig.enableCalibration = config.enableCalibration;
        processorConfig.numThreads = config.processingThreads;
        processorConfig.maxQueueSize = config.frameBufferSize;

        frameProcessor_ = std::make_unique<FrameProcessor>(processorConfig);

        // Initialize shared memory if enabled
        if (config.enableSharedMemory) {
            sharedMemory_ = std::make_unique<SharedMemory>(
                config.sharedMemoryName, config.sharedMemorySize, true);

            if (const auto status = sharedMemory_->initialize(); status != SharedMemory::Status::OK) {
                return Status::COMMUNICATION_ERROR;
            }
        }

        // Initialize gRPC server if enabled
        if (config.enableGrpc) {
            grpcServer_ = std::make_unique<GrpcServer>(
                config.grpcServerAddress, config.grpcServerPort);

            // Set up the frame provider
            grpcServer_->setFrameProvider([this]() -> std::shared_ptr<Frame> {
                // Get the latest frame from the buffer
                std::unique_lock<std::mutex> lock(frameBufferMutex_);
                if (frameBuffer_.empty()) {
                    return nullptr;
                }

                return frameBuffer_[frameBufferHead_];
            });

            // Set up the device control handler
            grpcServer_->setDeviceControlHandler(
                [this](const std::string &command, const std::string &params) -> bool {
                    // Handle commands
                    if (command == "start") {
                        return start() == Status::OK;
                    }
                    if (command == "stop") {
                        return stop() == Status::OK;
                    }
                    return false;
                });

            if (const auto status = grpcServer_->start(); status != GrpcServer::Status::OK) {
                return Status::COMMUNICATION_ERROR;
            }
        }

        // Initialize frame buffer
        frameBuffer_.resize(config.frameBufferSize);
        frameBufferHead_ = 0;
        frameBufferTail_ = 0;

        isInitialized_ = true;

        return Status::OK;
    }

    ImagingService::Status ImagingService::start() {
        if (!isInitialized_) {
            return Status::NOT_INITIALIZED;
        }

        if (isRunning_) {
            return Status::ALREADY_RUNNING;
        }

        // Start the frame processor
        frameProcessor_->start();

        // Set up the frame callback on the device
        const auto status = device_->startCapture(
            [this](const std::shared_ptr<Frame> &frame) {
                handleNewFrame(frame);
            });

        if (status != UltrasoundDevice::Status::OK) {
            frameProcessor_->stop();
            return Status::DEVICE_ERROR;
        }

        // Set up frame processor callback
        frameProcessor_->setFrameCallback(
            [this](const std::shared_ptr<Frame>& frame) {
                // Add to the frame buffer
                {
                    std::unique_lock<std::mutex> lock(frameBufferMutex_);

                    // Store the frame in the buffer
                    frameBuffer_[frameBufferTail_] = frame;

                    // Check if we need to overwrite frames (truly dropped)
                    const bool frameOverwritten = (frameBufferTail_ + 1) % frameBuffer_.size() == frameBufferHead_;

                    // Advance the tail
                    frameBufferTail_ = (frameBufferTail_ + 1) % frameBuffer_.size();

                    // If the buffer was full, advance the head and count the drop
                    if (frameOverwritten) {
                        frameBufferHead_ = (frameBufferHead_ + 1) % frameBuffer_.size();
                        ++droppedFrames_;
                    }
                }

                // Write to shared memory if enabled
                if (sharedMemory_ && sharedMemory_->isInitialized()) {
                    sharedMemory_->writeFrame(frame);
                }

                // Call the user callback if set
                if (frameCallback_) {
                    frameCallback_(frame);
                }
            });

        // Start the processing thread
        stopRequested_ = false;
        isRunning_ = true;
        startTime_ = std::chrono::system_clock::now();

        return Status::OK;
    }

    ImagingService::Status ImagingService::stop() {
        if (!isRunning_) {
            return Status::NOT_RUNNING;
        }

        // Stop the device
        auto status = device_->stopCapture();
        if (status != UltrasoundDevice::Status::OK) {
            return Status::DEVICE_ERROR;
        }

        // Stop the frame processor
        frameProcessor_->stop();

        // Set flag
        isRunning_ = false;

        return Status::OK;
    }

    bool ImagingService::isRunning() const {
        return isRunning_;
    }

    ImagingService::Config ImagingService::getConfig() const {
        return config_;
    }

    ImagingService::Status ImagingService::setFrameCallback(
        const std::function<void(std::shared_ptr<Frame>)> &callback) {
        frameCallback_ = callback;
        return Status::OK;
    }

    std::map<std::string, std::string> ImagingService::getStatistics() const {
        std::map<std::string, std::string> stats;

        stats["frame_count"] = std::to_string(frameCount_);
        stats["dropped_frames"] = std::to_string(droppedFrames_);

        // Calculate FPS
        auto now = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_);

        if (duration.count() > 0) {
            double fps = static_cast<double>(frameCount_) / duration.count();
            stats["average_fps"] = std::to_string(fps);
        } else {
            stats["average_fps"] = "0.0";
        }

        // Add processor statistics
        if (frameProcessor_) {
            auto processorStats = frameProcessor_->getStatistics();
            stats.insert(processorStats.begin(), processorStats.end());
        }

        return stats;
    }

    std::vector<std::string> ImagingService::getAvailableDevices() {
        auto &deviceManager = DeviceManager::getInstance();
        return deviceManager.getAvailableDeviceIds();
    }

    int ImagingService::registerDeviceChangeCallback(
        const std::function<void(const std::string &, bool)> &callback) {
        auto &deviceManager = DeviceManager::getInstance();
        return deviceManager.registerDeviceChangeCallback(callback);
    }

    bool ImagingService::unregisterDeviceChangeCallback(const int subscriptionId) {
        auto &deviceManager = DeviceManager::getInstance();
        return deviceManager.unregisterDeviceChangeCallback(subscriptionId);
    }

    void ImagingService::handleNewFrame(const std::shared_ptr<Frame>& frame) {
        if (!frame) {
            return;
        }

        // Increment the frame count
        ++frameCount_;

        // Queue the frame for processing
        if (frameProcessor_) {
            frameProcessor_->queueFrame(frame);
        }
    }
} // namespace medical::imaging
