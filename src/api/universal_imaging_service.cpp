#include "api/universal_imaging_service.h"
#include "device/imaging_device_adapter.h"
#include <iostream>
#include <sstream>
#include <fstream>

namespace medical::imaging {

UniversalImagingService::UniversalImagingService()
    : activeServiceType_(ImagingDeviceAdapter::DeviceType::UNKNOWN) {
}

UniversalImagingService::~UniversalImagingService() {
    // Stop if running
    if (isRunning()) {
        stop();
    }
}

UniversalImagingService::Status UniversalImagingService::initialize(const Config &config) {
    config_ = config;
    
    // Get device manager
    auto &deviceManager = ImagingDeviceManager::getInstance();
    
    // Determine device type and create appropriate service
    if (config.deviceId.empty()) {
        // Auto-select device
        std::vector<std::string> blackmagicDevices;
        std::vector<std::string> webcamDevices;
        
        if (!config.preferWebcams) {
            // Try Blackmagic first, then webcams
            blackmagicDevices = deviceManager.getDevicesByType(ImagingDeviceAdapter::DeviceType::BLACKMAGIC);
            webcamDevices = deviceManager.getDevicesByType(ImagingDeviceAdapter::DeviceType::WEBCAM);
        } else {
            // Try webcams first, then Blackmagic
            webcamDevices = deviceManager.getDevicesByType(ImagingDeviceAdapter::DeviceType::WEBCAM);
            blackmagicDevices = deviceManager.getDevicesByType(ImagingDeviceAdapter::DeviceType::BLACKMAGIC);
        }
        
        if (!blackmagicDevices.empty() && !config.preferWebcams) {
            // Use first Blackmagic device
            config_.deviceId = blackmagicDevices[0];
            activeServiceType_ = ImagingDeviceAdapter::DeviceType::BLACKMAGIC;
            std::cout << "Automatically selected Blackmagic device: " << config_.deviceId << std::endl;
        } else if (!webcamDevices.empty()) {
            // Use first webcam device
            config_.deviceId = webcamDevices[0];
            activeServiceType_ = ImagingDeviceAdapter::DeviceType::WEBCAM;
            std::cout << "Automatically selected webcam device: " << config_.deviceId << std::endl;
        } else if (!blackmagicDevices.empty()) {
            // Fallback to Blackmagic device if no webcams found
            config_.deviceId = blackmagicDevices[0];
            activeServiceType_ = ImagingDeviceAdapter::DeviceType::BLACKMAGIC;
            std::cout << "Automatically selected Blackmagic device: " << config_.deviceId << std::endl;
        } else {
            // No devices found
            std::cerr << "No imaging devices found!" << std::endl;
            return Status::DEVICE_ERROR;
        }
    } else {
        // Use the specified device
        auto device = deviceManager.getDevice(config.deviceId);
        if (!device) {
            std::cerr << "Device not found: " << config.deviceId << std::endl;
            return Status::DEVICE_ERROR;
        }
        
        activeServiceType_ = device->getDeviceType();
    }
    
    // Initialize the appropriate service
    if (activeServiceType_ == ImagingDeviceAdapter::DeviceType::BLACKMAGIC) {
        // Use Blackmagic service
        if (!blackmagicService_) {
            blackmagicService_ = std::make_shared<ImagingService>();
        }
        
        auto serviceConfig = convertConfig(config_);
        serviceConfig.deviceId = config_.deviceId;
        
        auto status = blackmagicService_->initialize(serviceConfig);
        return convertStatus(status);
        
    } else if (activeServiceType_ == ImagingDeviceAdapter::DeviceType::WEBCAM) {
        // Use webcam service
        if (!webcamService_) {
            webcamService_ = std::make_shared<WebcamImagingService>();
        }
        
        auto serviceConfig = convertConfigWebcam(config_);
        serviceConfig.deviceId = config_.deviceId;
        
        auto status = webcamService_->initialize(serviceConfig);
        return convertStatus(status);
        
    } else {
        // Unknown device type
        std::cerr << "Unknown device type!" << std::endl;
        return Status::DEVICE_ERROR;
    }
}

UniversalImagingService::Status UniversalImagingService::start() {
    if (activeServiceType_ == ImagingDeviceAdapter::DeviceType::BLACKMAGIC) {
        if (!blackmagicService_) {
            return Status::NOT_INITIALIZED;
        }
        
        auto status = blackmagicService_->start();
        return convertStatus(status);
        
    } else if (activeServiceType_ == ImagingDeviceAdapter::DeviceType::WEBCAM) {
        if (!webcamService_) {
            return Status::NOT_INITIALIZED;
        }
        
        auto status = webcamService_->start();
        return convertStatus(status);
        
    } else {
        return Status::INTERNAL_ERROR;
    }
}

UniversalImagingService::Status UniversalImagingService::stop() {
    if (activeServiceType_ == ImagingDeviceAdapter::DeviceType::BLACKMAGIC) {
        if (!blackmagicService_) {
            return Status::NOT_INITIALIZED;
        }
        
        auto status = blackmagicService_->stop();
        return convertStatus(status);
        
    } else if (activeServiceType_ == ImagingDeviceAdapter::DeviceType::WEBCAM) {
        if (!webcamService_) {
            return Status::NOT_INITIALIZED;
        }
        
        auto status = webcamService_->stop();
        return convertStatus(status);
        
    } else {
        return Status::INTERNAL_ERROR;
    }
}

bool UniversalImagingService::isRunning() const {
    if (activeServiceType_ == ImagingDeviceAdapter::DeviceType::BLACKMAGIC) {
        return blackmagicService_ && blackmagicService_->isRunning();
    } else if (activeServiceType_ == ImagingDeviceAdapter::DeviceType::WEBCAM) {
        return webcamService_ && webcamService_->isRunning();
    } else {
        return false;
    }
}

UniversalImagingService::Config UniversalImagingService::getConfig() const {
    return config_;
}

UniversalImagingService::Status UniversalImagingService::setConfig(const Config &config) {
    if (isRunning()) {
        return Status::ALREADY_RUNNING;
    }
    
    // Store the config for later initialization
    config_ = config;
    
    return Status::OK;
}

UniversalImagingService::Status UniversalImagingService::setFrameCallback(
    const std::function<void(std::shared_ptr<Frame>)> &callback) {
    
    frameCallback_ = callback;
    
    if (activeServiceType_ == ImagingDeviceAdapter::DeviceType::BLACKMAGIC) {
        if (!blackmagicService_) {
            return Status::NOT_INITIALIZED;
        }
        
        auto status = blackmagicService_->setFrameCallback(callback);
        return convertStatus(status);
        
    } else if (activeServiceType_ == ImagingDeviceAdapter::DeviceType::WEBCAM) {
        if (!webcamService_) {
            return Status::NOT_INITIALIZED;
        }
        
        auto status = webcamService_->setFrameCallback(callback);
        return convertStatus(status);
        
    } else {
        return Status::INTERNAL_ERROR;
    }
}

UniversalImagingService::PerformanceMetrics UniversalImagingService::getPerformanceMetrics() const {
    if (activeServiceType_ == ImagingDeviceAdapter::DeviceType::BLACKMAGIC) {
        if (!blackmagicService_) {
            return {};
        }
        
        return blackmagicService_->getPerformanceMetrics();
        
    } else if (activeServiceType_ == ImagingDeviceAdapter::DeviceType::WEBCAM) {
        if (!webcamService_) {
            return {};
        }
        
        // Explicit conversion between types
        auto webcamMetrics = webcamService_->getPerformanceMetrics();
        ImagingService::PerformanceMetrics result;

        // Copy the metrics fields
        result.frameCount = webcamMetrics.frameCount;
        result.droppedFrames = webcamMetrics.droppedFrames;
        result.averageFps = webcamMetrics.averageFps;
        result.currentFps = webcamMetrics.currentFps;
        result.averageLatencyMs = webcamMetrics.averageLatencyMs;
        result.maxLatencyMs = webcamMetrics.maxLatencyMs;
        result.cpuUsagePercent = webcamMetrics.cpuUsagePercent;
        result.memoryUsageMb = webcamMetrics.memoryUsageMb;
        result.uptime = webcamMetrics.uptime;

        return result;
    } else {
        return {};
    }
}

void UniversalImagingService::resetPerformanceMetrics() {
    if (activeServiceType_ == ImagingDeviceAdapter::DeviceType::BLACKMAGIC) {
        if (blackmagicService_) {
            blackmagicService_->resetPerformanceMetrics();
        }
    } else if (activeServiceType_ == ImagingDeviceAdapter::DeviceType::WEBCAM) {
        if (webcamService_) {
            webcamService_->resetPerformanceMetrics();
        }
    }
}

std::map<std::string, std::string> UniversalImagingService::getStatistics() const {
    if (activeServiceType_ == ImagingDeviceAdapter::DeviceType::BLACKMAGIC) {
        if (!blackmagicService_) {
            return {};
        }

        auto stats = blackmagicService_->getStatistics();
        stats["service_type"] = "blackmagic";
        return stats;

    } else if (activeServiceType_ == ImagingDeviceAdapter::DeviceType::WEBCAM) {
        if (!webcamService_) {
            return {};
        }

        auto stats = webcamService_->getStatistics();
        stats["service_type"] = "webcam";
        return stats;

    } else {
        std::map<std::string, std::string> stats;
        stats["service_type"] = "unknown";
        return stats;
    }
}

std::vector<std::string> UniversalImagingService::getAvailableDevices() {
    auto &deviceManager = ImagingDeviceManager::getInstance();
    return deviceManager.getAvailableDeviceIds();
}

std::map<ImagingDeviceAdapter::DeviceType, std::vector<std::string>> UniversalImagingService::getAvailableDevicesByType() {
    std::map<ImagingDeviceAdapter::DeviceType, std::vector<std::string>> devicesByType;

    auto &deviceManager = ImagingDeviceManager::getInstance();

    // Get devices by type
    devicesByType[ImagingDeviceAdapter::DeviceType::BLACKMAGIC] =
        deviceManager.getDevicesByType(ImagingDeviceAdapter::DeviceType::BLACKMAGIC);

    devicesByType[ImagingDeviceAdapter::DeviceType::WEBCAM] =
        deviceManager.getDevicesByType(ImagingDeviceAdapter::DeviceType::WEBCAM);

    return devicesByType;
}

std::vector<ImagingDeviceAdapter::DeviceInfo> UniversalImagingService::getAvailableDeviceInfo() {
    auto &deviceManager = ImagingDeviceManager::getInstance();
    return deviceManager.getAvailableDeviceInfo();
}

std::shared_ptr<SharedMemory> UniversalImagingService::getSharedMemory() const {
    if (activeServiceType_ == ImagingDeviceAdapter::DeviceType::BLACKMAGIC) {
        if (!blackmagicService_) {
            return nullptr;
        }

        return blackmagicService_->getSharedMemory();

    } else if (activeServiceType_ == ImagingDeviceAdapter::DeviceType::WEBCAM) {
        if (!webcamService_) {
            return nullptr;
        }

        return webcamService_->getSharedMemory();

    } else {
        return nullptr;
    }
}

bool UniversalImagingService::dumpDiagnostics(const std::string &filePath) const {
    if (activeServiceType_ == ImagingDeviceAdapter::DeviceType::BLACKMAGIC) {
        if (!blackmagicService_) {
            return false;
        }

        return blackmagicService_->dumpDiagnostics(filePath);

    } else if (activeServiceType_ == ImagingDeviceAdapter::DeviceType::WEBCAM) {
        if (!webcamService_) {
            return false;
        }

        // Use a simple wrapper approach to handle potential differences
        try {
            std::ofstream outFile(filePath);
            if (!outFile.is_open()) {
                return false;
            }

            // Write a header indicating this is from the universal service with webcam
            auto now = std::chrono::system_clock::now();
            auto nowTime = std::chrono::system_clock::to_time_t(now);
            outFile << "Universal Imaging Service Diagnostic Report (Webcam): " << std::ctime(&nowTime) << std::endl;

            // Get statistics and write them to the file
            auto stats = getStatistics();
            for (const auto &[key, value] : stats) {
                outFile << key << ": " << value << std::endl;
            }

            outFile.close();
            return true;
        } catch (...) {
            return false;
        }
        
    } else {
        try {
            std::ofstream outFile(filePath);
            if (!outFile.is_open()) {
                return false;
            }
            
            outFile << "No active service!" << std::endl;
            outFile.close();
            return true;
        } catch (...) {
            return false;
        }
    }
}

ImagingDeviceAdapter::DeviceType UniversalImagingService::getActiveServiceType() const {
    return activeServiceType_;
}

// Static helper methods
UniversalImagingService::Status UniversalImagingService::convertStatus(ImagingService::Status status) {
    switch (status) {
        case ImagingService::Status::OK:
            return Status::OK;
        case ImagingService::Status::DEVICE_ERROR:
            return Status::DEVICE_ERROR;
        case ImagingService::Status::PROCESSING_ERROR:
            return Status::PROCESSING_ERROR;
        case ImagingService::Status::COMMUNICATION_ERROR:
            return Status::COMMUNICATION_ERROR;
        case ImagingService::Status::NOT_INITIALIZED:
            return Status::NOT_INITIALIZED;
        case ImagingService::Status::ALREADY_RUNNING:
            return Status::ALREADY_RUNNING;
        case ImagingService::Status::INVALID_ARGUMENT:
            return Status::INVALID_ARGUMENT;
        case ImagingService::Status::NOT_RUNNING:
            return Status::NOT_RUNNING;
        case ImagingService::Status::INTERNAL_ERROR:
            return Status::INTERNAL_ERROR;
        case ImagingService::Status::TIMEOUT:
            return Status::TIMEOUT;
        default:
            return Status::INTERNAL_ERROR;
    }
}

UniversalImagingService::Status UniversalImagingService::convertStatus(WebcamImagingService::Status status) {
    switch (status) {
        case WebcamImagingService::Status::OK:
            return Status::OK;
        case WebcamImagingService::Status::DEVICE_ERROR:
            return Status::DEVICE_ERROR;
        case WebcamImagingService::Status::PROCESSING_ERROR:
            return Status::PROCESSING_ERROR;
        case WebcamImagingService::Status::COMMUNICATION_ERROR:
            return Status::COMMUNICATION_ERROR;
        case WebcamImagingService::Status::NOT_INITIALIZED:
            return Status::NOT_INITIALIZED;
        case WebcamImagingService::Status::ALREADY_RUNNING:
            return Status::ALREADY_RUNNING;
        case WebcamImagingService::Status::INVALID_ARGUMENT:
            return Status::INVALID_ARGUMENT;
        case WebcamImagingService::Status::NOT_RUNNING:
            return Status::NOT_RUNNING;
        case WebcamImagingService::Status::INTERNAL_ERROR:
            return Status::INTERNAL_ERROR;
        case WebcamImagingService::Status::TIMEOUT:
            return Status::TIMEOUT;
        default:
            return Status::INTERNAL_ERROR;
    }
}

ImagingService::Config UniversalImagingService::convertConfig(const Config &config) {
    ImagingService::Config serviceConfig;
    
    serviceConfig.deviceId = config.deviceId;
    serviceConfig.enableDirectMemoryAccess = config.enableDirectMemoryAccess;
    serviceConfig.useRealtimePriority = config.useRealtimePriority;
    serviceConfig.threadAffinity = config.threadAffinity;
    serviceConfig.pinMemory = config.pinMemory;
    serviceConfig.enableSharedMemory = config.enableSharedMemory;
    serviceConfig.sharedMemoryName = config.sharedMemoryName;
    serviceConfig.sharedMemorySize = config.sharedMemorySize;
    serviceConfig.sharedMemoryType = config.sharedMemoryType;
    serviceConfig.frameBufferSize = config.frameBufferSize;
    serviceConfig.dropFramesWhenFull = config.dropFramesWhenFull;
    serviceConfig.enablePerformanceMonitoring = config.enablePerformanceMonitoring;
    serviceConfig.logPerformanceStats = config.logPerformanceStats;
    serviceConfig.performanceLogIntervalMs = config.performanceLogIntervalMs;
    serviceConfig.maxFrameSize = config.maxFrameSize;
    
    // Convert device config
    serviceConfig.deviceConfig.width = config.deviceConfig.width;
    serviceConfig.deviceConfig.height = config.deviceConfig.height;
    serviceConfig.deviceConfig.frameRate = config.deviceConfig.frameRate;
    serviceConfig.deviceConfig.pixelFormat = config.deviceConfig.pixelFormat;
    serviceConfig.deviceConfig.enableAudio = config.deviceConfig.enableAudio;
    
    return serviceConfig;
}

WebcamImagingService::Config UniversalImagingService::convertConfigWebcam(const Config &config) {
    WebcamImagingService::Config serviceConfig;
    
    serviceConfig.deviceId = config.deviceId;
    serviceConfig.enableDirectMemoryAccess = config.enableDirectMemoryAccess;
    serviceConfig.useRealtimePriority = config.useRealtimePriority;
    serviceConfig.threadAffinity = config.threadAffinity;
    serviceConfig.pinMemory = config.pinMemory;
    serviceConfig.enableSharedMemory = config.enableSharedMemory;
    serviceConfig.sharedMemoryName = config.sharedMemoryName;
    serviceConfig.sharedMemorySize = config.sharedMemorySize;
    serviceConfig.sharedMemoryType = config.sharedMemoryType;
    serviceConfig.frameBufferSize = config.frameBufferSize;
    serviceConfig.dropFramesWhenFull = config.dropFramesWhenFull;
    serviceConfig.enablePerformanceMonitoring = config.enablePerformanceMonitoring;
    serviceConfig.logPerformanceStats = config.logPerformanceStats;
    serviceConfig.performanceLogIntervalMs = config.performanceLogIntervalMs;
    serviceConfig.maxFrameSize = config.maxFrameSize;
    
    // Convert device config
    serviceConfig.deviceConfig.width = config.deviceConfig.width;
    serviceConfig.deviceConfig.height = config.deviceConfig.height;
    serviceConfig.deviceConfig.frameRate = config.deviceConfig.frameRate;
    serviceConfig.deviceConfig.pixelFormat = config.deviceConfig.pixelFormat;
    serviceConfig.deviceConfig.enableAudio = config.deviceConfig.enableAudio;
    
    return serviceConfig;
}

} // namespace medical::imaging