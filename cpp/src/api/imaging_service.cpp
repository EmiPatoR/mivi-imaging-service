#include "api/imaging_service.h"
#include <chrono>
#include <thread>
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sys/resource.h>
#include <pthread.h>

namespace medical::imaging {
    // Performance monitoring constants
    constexpr size_t FPS_HISTORY_SIZE = 60; // 1 minute of history at 1Hz sampling
    constexpr size_t LATENCY_HISTORY_SIZE = 300; // 5 minutes of history

    ImagingService::ImagingService()
        : isInitialized_(false),
          isRunning_(false),
          stopRequested_(false),
          frameBufferHead_(0),
          frameBufferTail_(0),
          frameCount_(0),
          droppedFrames_(0) {
        // Initialize metrics
        metrics_ = {};
        startTime_ = std::chrono::system_clock::now();
        lastFrameTime_ = startTime_;
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

        // Setup device
        Status deviceStatus = setupDevice();
        if (deviceStatus != Status::OK) {
            return deviceStatus;
        }

        // Setup shared memory if enabled
        if (config.enableSharedMemory) {
            Status shmStatus = setupSharedMemory();
            if (shmStatus != Status::OK) {
                return shmStatus;
            }
        }

        // Initialize frame buffer
        frameBuffer_.resize(config.frameBufferSize);
        frameBufferHead_ = 0;
        frameBufferTail_ = 0;

        // Initialize performance metrics
        fpsHistory_.clear();
        latencyHistory_.clear();
        resetPerformanceMetrics();

        isInitialized_ = true;
        return Status::OK;
    }

    ImagingService::Status ImagingService::setupDevice() {
        // Get the device
        auto &deviceManager = DeviceManager::getInstance();

        if (config_.deviceId.empty()) {
            // Auto-select the first available device
            auto deviceIds = deviceManager.getAvailableDeviceIds();
            if (deviceIds.empty()) {
                return Status::DEVICE_ERROR;
            }

            device_ = deviceManager.getDevice(deviceIds[0]);
        } else {
            // Use the specified device
            device_ = deviceManager.getDevice(config_.deviceId);
        }

        if (!device_) {
            return Status::DEVICE_ERROR;
        }

        // Set up device configuration
        BlackmagicDevice::Config deviceConfig = config_.deviceConfig;

        // Apply additional configuration options
        deviceConfig.enableDirectMemoryAccess = config_.enableDirectMemoryAccess;

        // If using shared memory, configure direct output if possible
        if (config_.enableSharedMemory && device_->supportsFeature(DeviceFeature::DIRECT_MEMORY_ACCESS)) {
            deviceConfig.sharedMemoryName = config_.sharedMemoryName;
        }

        // Initialize the device
        if (device_->initialize(deviceConfig) != BlackmagicDevice::Status::OK) {
            return Status::DEVICE_ERROR;
        }

        return Status::OK;
    }

    // Static methods using the DeviceManager directly
    std::vector<std::string> ImagingService::getAvailableDevices() {
        auto &deviceManager = DeviceManager::getInstance();
        return deviceManager.getAvailableDeviceIds();
    }

    int ImagingService::registerDeviceChangeCallback(
        const std::function<void(const std::string &, bool)> &callback) {
        auto &deviceManager = DeviceManager::getInstance();
        return deviceManager.registerDeviceChangeCallback(callback);
    }

    bool ImagingService::unregisterDeviceChangeCallback(int subscriptionId) {
        auto &deviceManager = DeviceManager::getInstance();
        return deviceManager.unregisterDeviceChangeCallback(subscriptionId);
    }

    ImagingService::Status ImagingService::setupSharedMemory() {
        try {
            // Create shared memory configuration
            SharedMemory::Config shmConfig;
            shmConfig.name = config_.sharedMemoryName;
            shmConfig.size = config_.sharedMemorySize;
            shmConfig.type = config_.sharedMemoryType;
            shmConfig.create = true; // We are the producer
            shmConfig.maxFrames = config_.frameBufferSize;
            shmConfig.useHugePages = false; // Can be made configurable
            shmConfig.lockInMemory = config_.pinMemory;

            // Create the shared memory object
            sharedMemory_ = std::make_shared<SharedMemory>(shmConfig);

            // Initialize it
            auto status = sharedMemory_->initialize();
            if (status != SharedMemory::Status::OK) {
                std::cerr << "Failed to initialize shared memory: " << static_cast<int>(status) << std::endl;
                return Status::COMMUNICATION_ERROR;
            }

            // Set thread affinity and priority if configured
            if (config_.threadAffinity >= 0) {
                sharedMemory_->setThreadAffinity(config_.threadAffinity);
            }

            if (config_.useRealtimePriority) {
                sharedMemory_->setThreadPriority(10); // Medium-high priority
            }

            // Lock memory if requested
            if (config_.pinMemory) {
                sharedMemory_->lockMemory();
            }

            return Status::OK;
        } catch (const std::exception &e) {
            std::cerr << "Exception in setupSharedMemory: " << e.what() << std::endl;
            return Status::COMMUNICATION_ERROR;
        }
    }

    ImagingService::Status ImagingService::start() {
        if (!isInitialized_) {
            return Status::NOT_INITIALIZED;
        }

        if (isRunning_) {
            return Status::ALREADY_RUNNING;
        }

        // Reset counters and start time
        frameCount_ = 0;
        droppedFrames_ = 0;
        startTime_ = std::chrono::system_clock::now();
        lastFrameTime_ = startTime_;

        // Start performance monitoring if enabled
        if (config_.enablePerformanceMonitoring) {
            stopRequested_ = false;
            performanceThread_ = std::thread(&ImagingService::performanceMonitorThread, this);

            // Set thread priority and affinity if configured
            if (config_.useRealtimePriority) {
                setThreadPriority(performanceThread_, false, 5); // Lower priority than capture
            }

            if (config_.threadAffinity >= 0) {
                setThreadAffinity(performanceThread_, config_.threadAffinity);
            }
        }

        // Set up the frame callback on the device
        const auto status = device_->startCapture(
            [this](const std::shared_ptr<Frame> &frame) {
                handleNewFrame(frame);
            });

        if (status != BlackmagicDevice::Status::OK) {
            // Clean up performance thread if it was started
            if (config_.enablePerformanceMonitoring) {
                stopRequested_ = true;
                if (performanceThread_.joinable()) {
                    performanceThread_.join();
                }
            }

            return Status::DEVICE_ERROR;
        }

        isRunning_ = true;
        return Status::OK;
    }

    ImagingService::Status ImagingService::stop() {
        if (!isRunning_) {
            return Status::NOT_RUNNING;
        }

        // Stop the device
        auto status = device_->stopCapture();
        if (status != BlackmagicDevice::Status::OK) {
            return Status::DEVICE_ERROR;
        }

        // Stop performance monitoring thread
        if (config_.enablePerformanceMonitoring) {
            stopRequested_ = true;
            if (performanceThread_.joinable()) {
                performanceThread_.join();
            }
        }

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

    ImagingService::Status ImagingService::setConfig(const Config &config) {
        // Can only change config when not running
        if (isRunning_) {
            return Status::ALREADY_RUNNING;
        }

        config_ = config;

        // If already initialized, reinitialize with new config
        if (isInitialized_) {
            isInitialized_ = false;
            return initialize(config);
        }

        return Status::OK;
    }

    ImagingService::Status ImagingService::setFrameCallback(
        const std::function<void(std::shared_ptr<Frame>)> &callback) {
        frameCallback_ = callback;
        return Status::OK;
    }

    ImagingService::PerformanceMetrics ImagingService::getPerformanceMetrics() const {
        std::lock_guard<std::mutex> lock(metricsMutex_);
        return metrics_;
    }

    void ImagingService::resetPerformanceMetrics() {
        std::lock_guard<std::mutex> lock(metricsMutex_);

        metrics_ = {};
        startTime_ = std::chrono::system_clock::now();
        fpsHistory_.clear();
        latencyHistory_.clear();

        // Reset counters
        frameCount_ = 0;
        droppedFrames_ = 0;
    }

    std::map<std::string, std::string> ImagingService::getStatistics() const {
        std::map<std::string, std::string> stats;

        // Get basic stats
        stats["frame_count"] = std::to_string(frameCount_);
        stats["dropped_frames"] = std::to_string(droppedFrames_);

        // Get performance metrics
        PerformanceMetrics metrics = getPerformanceMetrics();
        stats["average_fps"] = std::to_string(metrics.averageFps);
        stats["current_fps"] = std::to_string(metrics.currentFps);
        stats["average_latency_ms"] = std::to_string(metrics.averageLatencyMs);
        stats["max_latency_ms"] = std::to_string(metrics.maxLatencyMs);
        stats["cpu_usage_percent"] = std::to_string(metrics.cpuUsagePercent);
        stats["memory_usage_mb"] = std::to_string(metrics.memoryUsageMb);
        stats["uptime_seconds"] = std::to_string(metrics.uptime.count());

        // Add shared memory stats if enabled
        if (sharedMemory_) {
            auto shmStats = sharedMemory_->getStatistics();
            stats["shm_frames_written"] = std::to_string(shmStats.totalFramesWritten);
            stats["shm_frames_read"] = std::to_string(shmStats.totalFramesRead);
            stats["shm_dropped_frames"] = std::to_string(shmStats.droppedFrames);
            stats["shm_avg_write_latency_ns"] = std::to_string(shmStats.writeLatencyNsAvg);
            stats["shm_avg_read_latency_ns"] = std::to_string(shmStats.readLatencyNsAvg);
            stats["shm_peak_memory_usage"] = std::to_string(shmStats.peakMemoryUsage);
            stats["shm_current_frame_count"] = std::to_string(sharedMemory_->getCurrentFrameCount());
            stats["shm_is_buffer_full"] = sharedMemory_->isBufferFull() ? "true" : "false";
        }

        // Add device diagnostics
        if (device_) {
            auto deviceDiagnostics = device_->getDiagnostics();
            for (const auto &[key, value]: deviceDiagnostics) {
                stats["device_" + key] = value;
            }
        }

        return stats;
    }

    std::shared_ptr<SharedMemory> ImagingService::getSharedMemory() const {
        return sharedMemory_;
    }

    bool ImagingService::dumpDiagnostics(const std::string &filePath) const {
        try {
            std::ofstream outFile(filePath);
            if (!outFile.is_open()) {
                return false;
            }

            // Write timestamp
            auto now = std::chrono::system_clock::now();
            auto nowTime = std::chrono::system_clock::to_time_t(now);
            outFile << "Diagnostic Report: " << std::ctime(&nowTime) << std::endl;

            // Write service info
            outFile << "=== Service Information ===" << std::endl;
            outFile << "Running: " << (isRunning_ ? "Yes" : "No") << std::endl;
            outFile << "Initialized: " << (isInitialized_ ? "Yes" : "No") << std::endl;

            // Write configuration
            outFile << std::endl << "=== Configuration ===" << std::endl;
            outFile << "Device ID: " << config_.deviceId << std::endl;
            outFile << "Shared Memory: " << (config_.enableSharedMemory ? "Enabled" : "Disabled") << std::endl;
            if (config_.enableSharedMemory) {
                outFile << "Shared Memory Name: " << config_.sharedMemoryName << std::endl;
                outFile << "Shared Memory Size: " << config_.sharedMemorySize << " bytes" << std::endl;
            }
            outFile << "Frame Buffer Size: " << config_.frameBufferSize << std::endl;
            outFile << "Realtime Priority: " << (config_.useRealtimePriority ? "Enabled" : "Disabled") << std::endl;
            outFile << "Thread Affinity: " << config_.threadAffinity << std::endl;

            // Write statistics
            outFile << std::endl << "=== Statistics ===" << std::endl;
            auto stats = getStatistics();
            for (const auto &[key, value]: stats) {
                outFile << key << ": " << value << std::endl;
            }

            // Write device info
            if (device_) {
                outFile << std::endl << "=== Device Information ===" << std::endl;
                outFile << "Device ID: " << device_->getDeviceId() << std::endl;
                outFile << "Device Name: " << device_->getDeviceName() << std::endl;
                outFile << "Device Model: " << device_->getDeviceModel() << std::endl;

                auto capabilities = device_->getCapabilities();
                outFile << "DMA Support: " << (capabilities.supportsDma ? "Yes" : "No") << std::endl;
                outFile << "GPU Direct Support: " << (capabilities.supportsGpuDirect ? "Yes" : "No") << std::endl;
                outFile << "Hardware Timestamps: " << (capabilities.supportsHardwareTimestamps ? "Yes" : "No") <<
                        std::endl;

                outFile << "Supported Pixel Formats: ";
                for (const auto &format: capabilities.supportedPixelFormats) {
                    outFile << format << " ";
                }
                outFile << std::endl;

                outFile << "Current Frame Rate: " << device_->getCurrentFrameRate() << " fps" << std::endl;
            }

            outFile.close();
            return true;
        } catch (const std::exception &e) {
            std::cerr << "Exception in dumpDiagnostics: " << e.what() << std::endl;
            return false;
        }
    }

    void ImagingService::handleNewFrame(const std::shared_ptr<Frame> &frame) {
        if (!frame) {
            return;
        }

        // Record frame arrival time for latency calculation
        auto now = std::chrono::high_resolution_clock::now();

        // Increment the frame count
        ++frameCount_;

        // Calculate inter-frame time for FPS
        auto frameTime = std::chrono::system_clock::now();
        auto timeSinceLastFrame = std::chrono::duration_cast<std::chrono::microseconds>(
            frameTime - lastFrameTime_).count();
        lastFrameTime_ = frameTime;

        // Update performance metrics
        {
            std::lock_guard<std::mutex> lock(metricsMutex_);

            // Update FPS calculation
            if (timeSinceLastFrame > 0) {
                double instantFps = 1000000.0 / timeSinceLastFrame;
                fpsHistory_.push_back(instantFps);
                if (fpsHistory_.size() > FPS_HISTORY_SIZE) {
                    fpsHistory_.pop_front();
                }
            }

            // Calculate latency
            auto frameTimestamp = frame->getTimestamp();
            auto latencyUs = std::chrono::duration_cast<std::chrono::microseconds>(
                frameTime - frameTimestamp).count();

            latencyHistory_.push_back(latencyUs / 1000.0); // Convert to ms
            if (latencyHistory_.size() > LATENCY_HISTORY_SIZE) {
                latencyHistory_.pop_front();
            }
        }

        // Write to shared memory if enabled
        if (sharedMemory_ && sharedMemory_->isInitialized()) {
            auto status = sharedMemory_->writeFrame(frame);
            if (status != SharedMemory::Status::OK && status != SharedMemory::Status::BUFFER_FULL) {
                // Log error but continue - don't disrupt the capture flow
                std::cerr << "Failed to write frame to shared memory: " << static_cast<int>(status) << std::endl;
            }
        }

        // Update frame buffer if needed (minimal buffering)
        bool frameBufferFull = false; {
            std::lock_guard<std::mutex> lock(frameBufferMutex_);

            // Store the frame in the buffer
            frameBuffer_[frameBufferTail_] = frame;

            // Check if we need to overwrite frames
            bool frameOverwritten = (frameBufferTail_ + 1) % frameBuffer_.size() == frameBufferHead_;

            // Advance the tail
            frameBufferTail_ = (frameBufferTail_ + 1) % frameBuffer_.size();

            // If the buffer was full, advance the head and count the drop
            if (frameOverwritten) {
                frameBufferHead_ = (frameBufferHead_ + 1) % frameBuffer_.size();
                ++droppedFrames_;
                frameBufferFull = true;
            }
        }

        // Log buffer full condition if it occurred and logging is enabled
        if (frameBufferFull && config_.logPerformanceStats) {
            std::cerr << "Warning: Frame buffer full, oldest frame dropped" << std::endl;
        }

        // Call the user callback if set
        if (frameCallback_) {
            frameCallback_(frame);
        }
    }

    void ImagingService::performanceMonitorThread() {
        // Performance monitoring thread that periodically updates metrics
        // and optionally logs performance information

        // Track CPU usage
        struct rusage lastUsage;
        getrusage(RUSAGE_SELF, &lastUsage);
        auto lastCpuTime = lastUsage.ru_utime.tv_sec + lastUsage.ru_stime.tv_sec +
                           (lastUsage.ru_utime.tv_usec + lastUsage.ru_stime.tv_usec) / 1000000.0;
        auto lastTimestamp = std::chrono::steady_clock::now();

        while (!stopRequested_) {
            // Sleep for a short interval
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            // Skip if we're stopping
            if (stopRequested_) {
                break;
            }

            // Update performance metrics
            updatePerformanceMetrics();

            // Log performance statistics if enabled
            if (config_.logPerformanceStats) {
                // Only log every performanceLogIntervalMs ms
                static auto lastLogTime = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();

                if (std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - lastLogTime).count() >= config_.performanceLogIntervalMs) {
                    // Get current metrics
                    PerformanceMetrics metrics; {
                        std::lock_guard<std::mutex> lock(metricsMutex_);
                        metrics = metrics_;
                    }

                    // Log to console
                    std::cout << "Performance: "
                            << "FPS=" << std::fixed << std::setprecision(1) << metrics.currentFps
                            << " Latency=" << std::fixed << std::setprecision(2) << metrics.averageLatencyMs << "ms"
                            << " CPU=" << std::fixed << std::setprecision(1) << metrics.cpuUsagePercent << "%"
                            << " Mem=" << std::fixed << std::setprecision(1) << metrics.memoryUsageMb << "MB"
                            << " Frames=" << frameCount_
                            << " Dropped=" << droppedFrames_
                            << std::endl;

                    lastLogTime = now;
                }
            }
        }
    }

    void ImagingService::updatePerformanceMetrics() {
        std::lock_guard<std::mutex> lock(metricsMutex_);

        // Update uptime
        auto now = std::chrono::system_clock::now();
        metrics_.uptime = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_);

        // Update frame counts
        metrics_.frameCount = frameCount_;
        metrics_.droppedFrames = droppedFrames_;

        // Calculate average FPS
        if (metrics_.uptime.count() > 0) {
            metrics_.averageFps = static_cast<double>(frameCount_) / metrics_.uptime.count();
        } else {
            metrics_.averageFps = 0.0;
        }

        // Calculate current FPS from recent history
        if (!fpsHistory_.empty()) {
            metrics_.currentFps = std::accumulate(fpsHistory_.begin(), fpsHistory_.end(), 0.0) / fpsHistory_.size();
        } else {
            metrics_.currentFps = 0.0;
        }

        // Calculate latency statistics
        if (!latencyHistory_.empty()) {
            metrics_.averageLatencyMs = std::accumulate(latencyHistory_.begin(), latencyHistory_.end(), 0.0) /
                                        latencyHistory_.size();
            metrics_.maxLatencyMs = *std::max_element(latencyHistory_.begin(), latencyHistory_.end());
        } else {
            metrics_.averageLatencyMs = 0.0;
            metrics_.maxLatencyMs = 0.0;
        }

        // Get CPU usage
        struct rusage currentUsage;
        getrusage(RUSAGE_SELF, &currentUsage);

        double currentCpuTime = currentUsage.ru_utime.tv_sec + currentUsage.ru_stime.tv_sec +
                                (currentUsage.ru_utime.tv_usec + currentUsage.ru_stime.tv_usec) / 1000000.0;

        static double lastCpuTime = 0.0;
        static auto lastCpuCheckTime = std::chrono::steady_clock::now();

        auto currentTime = std::chrono::steady_clock::now();
        double elapsedSeconds = std::chrono::duration_cast<std::chrono::microseconds>(
                                    currentTime - lastCpuCheckTime).count() / 1000000.0;

        if (elapsedSeconds > 0) {
            double cpuSeconds = currentCpuTime - lastCpuTime;
            metrics_.cpuUsagePercent = (cpuSeconds / elapsedSeconds) * 100.0;

            // Clamp to valid range
            metrics_.cpuUsagePercent = std::max(0.0, std::min(100.0, metrics_.cpuUsagePercent));

            lastCpuTime = currentCpuTime;
            lastCpuCheckTime = currentTime;
        }

        // Get memory usage
        metrics_.memoryUsageMb = currentUsage.ru_maxrss / 1024.0; // Convert to MB
    }

    bool ImagingService::setThreadPriority(std::thread &thread, bool isRealtime, int priority) {
        // Set thread priority using POSIX APIs
        sched_param param;
        int policy;

        if (isRealtime) {
            policy = SCHED_RR; // Round-robin real-time scheduler
            param.sched_priority = priority;
        } else {
            policy = SCHED_OTHER; // Standard scheduler
            param.sched_priority = 0; // Ignored for SCHED_OTHER
        }

        // Get native thread handle
        pthread_t nativeHandle = thread.native_handle();

        // Set scheduler policy and priority
        int result = pthread_setschedparam(nativeHandle, policy, &param);

        if (result != 0) {
            std::cerr << "Failed to set thread priority: " << result << std::endl;
            return false;
        }

        // If not realtime but we still want to adjust nice value
        if (!isRealtime && priority != 0) {
            // Convert priority to nice value (inverted scale, -20 to 19)
            int niceValue = std::max(-20, std::min(19, -priority));

            // Set nice value
            if (setpriority(PRIO_PROCESS, 0, niceValue) != 0) {
                std::cerr << "Failed to set nice value: " << errno << std::endl;
                return false;
            }
        }

        return true;
    }

    bool ImagingService::setThreadAffinity(std::thread &thread, int cpuCore) {
        // Set thread affinity using POSIX APIs
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpuCore, &cpuset);

        // Get native thread handle
        pthread_t nativeHandle = thread.native_handle();

        // Set CPU affinity
        int result = pthread_setaffinity_np(nativeHandle, sizeof(cpu_set_t), &cpuset);

        if (result != 0) {
            std::cerr << "Failed to set thread affinity: " << result << std::endl;
            return false;
        }

        return true;
    }

    // ImagingServiceManager implementation
    ImagingServiceManager &ImagingServiceManager::getInstance() {
        static ImagingServiceManager instance;
        return instance;
    }

    std::shared_ptr<ImagingService> ImagingServiceManager::createService(const std::string &serviceName) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Check if service with this name already exists
        auto it = services_.find(serviceName);
        if (it != services_.end()) {
            std::cerr << "Service with name '" << serviceName << "' already exists" << std::endl;
            return it->second;
        }

        // Create new service
        auto service = std::make_shared<ImagingService>();
        services_[serviceName] = service;
        return service;
    }

    std::shared_ptr<ImagingService> ImagingServiceManager::getService(const std::string &serviceName) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = services_.find(serviceName);
        if (it != services_.end()) {
            return it->second;
        }

        return nullptr;
    }

    bool ImagingServiceManager::destroyService(const std::string &serviceName) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = services_.find(serviceName);
        if (it != services_.end()) {
            // Stop the service if it's running
            if (it->second->isRunning()) {
                it->second->stop();
            }

            // Remove it from the map
            services_.erase(it);
            return true;
        }

        return false;
    }

    void ImagingServiceManager::destroyAll() {
        std::lock_guard<std::mutex> lock(mutex_);

        for (auto &[name, service]: services_) {
            if (service->isRunning()) {
                service->stop();
            }
        }

        services_.clear();
    }

    ImagingServiceManager::~ImagingServiceManager() {
        destroyAll();
    }
} // namespace imaging::medical
