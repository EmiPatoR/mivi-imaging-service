#include "device/webcam_device.h"
#include <sstream>
#include <iostream>
#include <thread>
#include <algorithm>
#include <numeric>

namespace medical {
namespace imaging {

WebcamDevice::WebcamDevice(int deviceIndex)
    : deviceIndex_(deviceIndex),
      isCapturing_(false),
      stopRequested_(false),
      frameCount_(0),
      droppedFrames_(0) {
    
    // Generate device ID
    std::stringstream ss;
    ss << "webcam_" << deviceIndex;
    deviceId_ = ss.str();
    
    // Set default device name and model
    deviceName_ = "Webcam Device";
    deviceModel_ = "Generic Webcam";
    
    // Try to open the device to get more information
    cv::VideoCapture tempCamera(deviceIndex);
    if (tempCamera.isOpened()) {
        // Try to get device name (not always supported)
        deviceName_ = "Webcam " + std::to_string(deviceIndex);
        tempCamera.release();
    }
    
    // Initialize performance tracking
    startTime_ = std::chrono::steady_clock::now();
    lastFrameTime_ = startTime_;
    
    // Query device capabilities
    queryCapabilities();
}

WebcamDevice::~WebcamDevice() {
    // Stop capturing if necessary
    if (isCapturing_) {
        stopCapture();
    }
    
    // Release the camera
    if (camera_.isOpened()) {
        camera_.release();
    }
}

std::string WebcamDevice::getDeviceId() const {
    return deviceId_;
}

std::string WebcamDevice::getDeviceName() const {
    return deviceName_;
}

std::string WebcamDevice::getDeviceModel() const {
    return deviceModel_;
}

WebcamDevice::Status WebcamDevice::initialize(const Config &config) {
    // Check if already capturing
    if (isCapturing_) {
        stopCapture();
    }
    
    // If camera is open, close it
    if (camera_.isOpened()) {
        camera_.release();
    }
    
    // Try multiple backends to open the camera
    // CV_CAP_ANY = 0, which is already the default, but we can try specific backends
    bool cameraOpened = false;

    // Try V4L2 first (standard Linux API)
    camera_ = cv::VideoCapture(deviceIndex_, cv::CAP_V4L2);
    if (camera_.isOpened()) {
        std::cout << "Opened camera using V4L2 backend" << std::endl;
        cameraOpened = true;
    } else {
        // Try GSTREAMER next
        camera_ = cv::VideoCapture(deviceIndex_, cv::CAP_GSTREAMER);
        if (camera_.isOpened()) {
            std::cout << "Opened camera using GStreamer backend" << std::endl;
            cameraOpened = true;
        } else {
            // Default backend as a last resort
            camera_ = cv::VideoCapture(deviceIndex_);
            if (camera_.isOpened()) {
                std::cout << "Opened camera using default backend" << std::endl;
                cameraOpened = true;
            }
        }
    }

    if (!cameraOpened) {
        std::cerr << "Failed to open webcam device " << deviceIndex_ << std::endl;
        return Status::DEVICE_NOT_FOUND;
    }

    // Configure the camera - try setting properties multiple times as sometimes
    // the first attempt fails with some webcam drivers
    for (int attempt = 0; attempt < 3; attempt++) {
        camera_.set(cv::CAP_PROP_FRAME_WIDTH, config.width);
        camera_.set(cv::CAP_PROP_FRAME_HEIGHT, config.height);
        camera_.set(cv::CAP_PROP_FPS, config.frameRate);

        // Give the camera a moment to apply settings
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Verify settings
    int actualWidth = static_cast<int>(camera_.get(cv::CAP_PROP_FRAME_WIDTH));
    int actualHeight = static_cast<int>(camera_.get(cv::CAP_PROP_FRAME_HEIGHT));
    double actualFps = camera_.get(cv::CAP_PROP_FPS);

    std::cout << "Webcam configured with: "
              << actualWidth << "x" << actualHeight
              << " @ " << actualFps << "fps" << std::endl;

    // If we got 0x0 resolution or 0 fps, try to read a test frame to get actual values
    if (actualWidth == 0 || actualHeight == 0 || actualFps == 0) {
        cv::Mat testFrame;
        if (camera_.read(testFrame)) {
            actualWidth = testFrame.cols;
            actualHeight = testFrame.rows;

            // If FPS is still 0, use a reasonable default
            if (actualFps == 0) {
                actualFps = 30.0;
            }

            std::cout << "Webcam actual size from test frame: "
                     << actualWidth << "x" << actualHeight
                     << " @ " << actualFps << "fps" << std::endl;
        } else {
            std::cerr << "Failed to read test frame from webcam" << std::endl;
            camera_.release();
            return Status::CONFIGURATION_ERROR;
        }
    }

    // Store the configuration
    currentConfig_ = config;
    currentConfig_.width = actualWidth;
    currentConfig_.height = actualHeight;
    currentConfig_.frameRate = actualFps;

    return Status::OK;
}

WebcamDevice::Status WebcamDevice::startCapture(std::function<void(std::shared_ptr<Frame>)> frameCallback) {
    if (!camera_.isOpened()) {
        return Status::NOT_STREAMING;
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

    // Start the capture thread
    stopRequested_ = false;
    isCapturing_ = true;
    captureThread_ = std::thread(&WebcamDevice::captureThread, this);

    return Status::OK;
}

WebcamDevice::Status WebcamDevice::stopCapture() {
    if (!isCapturing_) {
        return Status::NOT_STREAMING;
    }

    // Signal the thread to stop
    stopRequested_ = true;

    // Wait for the thread to exit
    if (captureThread_.joinable()) {
        captureThread_.join();
    }

    isCapturing_ = false;
    return Status::OK;
}

bool WebcamDevice::isCapturing() const {
    return isCapturing_;
}

std::vector<WebcamDevice::Config> WebcamDevice::getSupportedConfigurations() const {
    std::vector<Config> configs;

    // Add configurations based on capabilities
    for (const auto& res : capabilities_.supportedResolutions) {
        for (const auto& fps : capabilities_.supportedFrameRates) {
            for (const auto& format : capabilities_.supportedPixelFormats) {
                Config config;
                config.width = res.first;
                config.height = res.second;
                config.frameRate = fps;
                config.pixelFormat = format;
                config.deviceIndex = deviceIndex_;

                configs.push_back(config);
            }
        }
    }

    return configs;
}

WebcamDevice::Config WebcamDevice::getCurrentConfiguration() const {
    return currentConfig_;
}

WebcamDevice::Capabilities WebcamDevice::getCapabilities() const {
    return capabilities_;
}

double WebcamDevice::getCurrentFrameRate() const {
    std::lock_guard<std::mutex> lock(metricsMutex_);

    if (fpsHistory_.empty()) {
        return 0.0;
    }

    // Calculate average FPS from recent history
    return std::accumulate(fpsHistory_.begin(), fpsHistory_.end(), 0.0) / fpsHistory_.size();
}

std::map<std::string, std::string> WebcamDevice::getDiagnostics() const {
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

    return diagnostics;
}

void WebcamDevice::captureThread() {
    cv::Mat frame;
    int failedFrameCount = 0;
    const int MAX_FAILED_FRAMES = 10;

    while (!stopRequested_) {
        // Capture a frame
        bool success = camera_.read(frame);

        if (!success || frame.empty()) {
            failedFrameCount++;
            std::cerr << "Failed to capture frame from webcam (attempt " << failedFrameCount << ")" << std::endl;

            if (failedFrameCount >= MAX_FAILED_FRAMES) {
                std::cerr << "Too many failed capture attempts, trying to reinitialize camera..." << std::endl;

                // Try to reinitialize the camera
                camera_.release();
                std::this_thread::sleep_for(std::chrono::milliseconds(500));

                // Try multiple backends
                bool reopened = false;

                // Try V4L2 first
                camera_ = cv::VideoCapture(deviceIndex_, cv::CAP_V4L2);
                if (camera_.isOpened()) {
                    reopened = true;
                } else {
                    // Try GSTREAMER next
                    camera_ = cv::VideoCapture(deviceIndex_, cv::CAP_GSTREAMER);
                    if (camera_.isOpened()) {
                        reopened = true;
                    } else {
                        // Default backend as a last resort
                        camera_ = cv::VideoCapture(deviceIndex_);
                        reopened = camera_.isOpened();
                    }
                }

                if (reopened) {
                    // Reconfigure the camera
                    camera_.set(cv::CAP_PROP_FRAME_WIDTH, currentConfig_.width);
                    camera_.set(cv::CAP_PROP_FRAME_HEIGHT, currentConfig_.height);
                    camera_.set(cv::CAP_PROP_FPS, currentConfig_.frameRate);

                    std::cout << "Camera reinitialized successfully" << std::endl;
                    failedFrameCount = 0;
                } else {
                    std::cerr << "Failed to reinitialize camera" << std::endl;
                    break; // Exit the capture thread if we can't recover
                }
            }

            // Sleep a bit before trying again
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Reset failed frame counter on success
        failedFrameCount = 0;

        // Increment frame count
        ++frameCount_;

        // Calculate inter-frame time for FPS
        auto frameTime = std::chrono::steady_clock::now();
        auto timeSinceLastFrame = std::chrono::duration_cast<std::chrono::microseconds>(
            frameTime - lastFrameTime_).count();
        lastFrameTime_ = frameTime;

        // Update FPS calculation
        {
            std::lock_guard<std::mutex> lock(metricsMutex_);

            if (timeSinceLastFrame > 0) {
                double instantFps = 1000000.0 / timeSinceLastFrame;
                fpsHistory_.push_back(instantFps);
                if (fpsHistory_.size() > 60) {  // Keep 1 second of history at 60fps
                    fpsHistory_.pop_front();
                }
            }
        }

        // Convert to our frame format
        auto ourFrame = convertOpenCVMatToFrame(frame);

        // Call the callback if set
        if (frameCallback_ && ourFrame) {
            frameCallback_(ourFrame);
        }
    }
}

bool WebcamDevice::queryCapabilities() {
    // Initialize with default values
    capabilities_.supportedPixelFormats = {"RGB", "BGR", "YUV"};

    // Open the camera to query capabilities
    cv::VideoCapture tempCamera(deviceIndex_);
    if (!tempCamera.isOpened()) {
        return false;
    }

    // Add common resolutions
    capabilities_.supportedResolutions = {
        {640, 480},   // VGA
        {800, 600},   // SVGA
        {1280, 720},  // HD
        {1920, 1080}  // Full HD
    };

    // Add common frame rates
    capabilities_.supportedFrameRates = {15.0, 30.0, 60.0};

    // Add device info
    capabilities_.deviceInfo["vendor"] = "Generic";
    capabilities_.deviceInfo["model"] = deviceModel_;
    capabilities_.deviceInfo["backend"] = "OpenCV";

    // Release the camera
    tempCamera.release();

    return true;
}

void WebcamDevice::updatePerformanceMetrics() {
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
            fpsHistory_.pop_front();
        }
    }
}

std::shared_ptr<Frame> WebcamDevice::convertOpenCVMatToFrame(const cv::Mat &mat) {
    if (mat.empty()) {
        return nullptr;
    }

    // Determine format and bytes per pixel
    std::string format;
    int bytesPerPixel;
    cv::Mat convertedMat;

    switch (mat.channels()) {
        case 1:
            format = "GRAY";
            bytesPerPixel = 1;
            convertedMat = mat.clone();
            break;
        case 3:
            // OpenCV uses BGR by default, convert to RGB for consistency with BlackmagicDevice
            format = "RGB";
            bytesPerPixel = 3;
            cv::cvtColor(mat, convertedMat, cv::COLOR_BGR2RGB);
            break;
        case 4:
            // Convert BGRA to RGBA
            format = "RGBA";
            bytesPerPixel = 4;
            cv::cvtColor(mat, convertedMat, cv::COLOR_BGRA2RGBA);
            break;
        default:
            std::cerr << "Unsupported number of channels: " << mat.channels() << std::endl;
            return nullptr;
    }

    // Make sure the data is continuous (no padding/stride issues)
    if (!convertedMat.isContinuous()) {
        convertedMat = convertedMat.clone();
    }

    // Create a frame with the correct dimensions
    auto frame = Frame::create(
        convertedMat.cols,
        convertedMat.rows,
        bytesPerPixel,
        format
    );

    if (!frame) {
        return nullptr;
    }

    // Get raw data and size
    size_t dataSize = convertedMat.total() * convertedMat.elemSize();

    // Copy the data
    std::memcpy(frame->getData(), convertedMat.data, dataSize);

    // Set up frame metadata
    frame->setTimestamp(std::chrono::system_clock::now());

    // Generate frame ID based on the timestamp
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    frame->setFrameId(static_cast<uint64_t>(nanos));

    // Set up enhanced metadata
    FrameMetadata &metadata = frame->getMetadataMutable();
    metadata.deviceId = deviceId_;
    metadata.width = convertedMat.cols;
    metadata.height = convertedMat.rows;
    metadata.bytesPerPixel = bytesPerPixel;
    metadata.frameNumber = frameCount_;
    metadata.hasBeenProcessed = false;
    metadata.signalStrength = 1.0f;  // Set a good signal quality for webcams
    metadata.signalToNoiseRatio = 40.0f;  // Reasonable SNR value
    metadata.confidenceScore = 1.0f;  // High confidence for webcam frames
    
    return frame;
}

// WebcamDeviceManager implementation
WebcamDeviceManager& WebcamDeviceManager::getInstance() {
    static WebcamDeviceManager instance;
    return instance;
}

WebcamDeviceManager::WebcamDeviceManager() {
    // Initial device discovery
    discoverDevices();
}

WebcamDeviceManager::~WebcamDeviceManager() = default;

int WebcamDeviceManager::discoverDevices() {
    std::cout << "Discovering webcam devices..." << std::endl;
    
    std::lock_guard<std::mutex> lock(mutex_);
    devices_.clear();
    
    int count = 0;
    const int MAX_DEVICES = 10;  // Check up to 10 devices
    
    for (int i = 0; i < MAX_DEVICES; ++i) {
        cv::VideoCapture camera(i);
        if (camera.isOpened()) {
            // Create device
            auto device = std::make_shared<WebcamDevice>(i);
            
            // Add to map
            devices_[device->getDeviceId()] = device;
            
            // Increment count
            count++;
            
            // Release the camera
            camera.release();
        }
    }
    
    std::cout << "Found " << count << " webcam devices" << std::endl;
    return count;
}

std::vector<std::string> WebcamDeviceManager::getAvailableDeviceIds() const {
    std::vector<std::string> ids;
    
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &pair : devices_) {
        ids.push_back(pair.first);
    }
    
    return ids;
}

std::shared_ptr<WebcamDevice> WebcamDeviceManager::getDevice(const std::string &deviceId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = devices_.find(deviceId);
    if (it != devices_.end()) {
        return it->second;
    }
    
    return nullptr;
}

} // namespace imaging
} // namespace medical