#include "api/imaging_service.h"
#include <iostream>
#include <string>
#include <csignal>
#include <thread>
#include <atomic>

// Global variables
std::atomic<bool> g_running(true);

// Signal handler
void signalHandler(int signal) {
    std::cout << "Received signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
}

int main(int argc, char *argv[]) {
    // Register signal handler
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "Medical Ultrasound Imaging Service" << std::endl;
    std::cout << "=================================" << std::endl;

    // Create the imaging service
    medical::imaging::ImagingService service;

    // Create configuration
    medical::imaging::ImagingService::Config config;

    // Process command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--device" && i + 1 < argc) {
            config.deviceId = argv[++i];
        } else if (arg == "--width" && i + 1 < argc) {
            config.deviceConfig.width = std::stoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            config.deviceConfig.height = std::stoi(argv[++i]);
        } else if (arg == "--frame-rate" && i + 1 < argc) {
            config.deviceConfig.frameRate = std::stod(argv[++i]);
        } else if (arg == "--pixel-format" && i + 1 < argc) {
            config.deviceConfig.pixelFormat = argv[++i];
        } else if (arg == "--no-shared-memory") {
            config.enableSharedMemory = false;
        } else if (arg == "--shared-memory-name" && i + 1 < argc) {
            config.sharedMemoryName = argv[++i];
        } else if (arg == "--shared-memory-size" && i + 1 < argc) {
            config.sharedMemorySize = std::stoull(argv[++i]);
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --device <id>              Device ID to use (default: auto-select)" << std::endl;
            std::cout << "  --width <pixels>           Frame width (default: 1920)" << std::endl;
            std::cout << "  --height <pixels>          Frame height (default: 1080)" << std::endl;
            std::cout << "  --frame-rate <fps>         Frame rate (default: 60.0)" << std::endl;
            std::cout << "  --pixel-format <format>    Pixel format (default: YUV)" << std::endl;
            std::cout << "  --no-segmentation          Disable thyroid segmentation" << std::endl;
            std::cout << "  --no-calibration           Disable probe calibration" << std::endl;
            std::cout << "  --threads <count>          Number of processing threads (default: 2)" << std::endl;
            std::cout << "  --no-shared-memory         Disable shared memory" << std::endl;
            std::cout << "  --shared-memory-name <name> Shared memory name (default: ultrasound_frames)" << std::endl;
            std::cout << "  --shared-memory-size <bytes> Shared memory size (default: 67108864)" << std::endl;
            std::cout << "  --no-grpc                  Disable gRPC server" << std::endl;
            std::cout << "  --grpc-address <address>   gRPC server address (default: 0.0.0.0)" << std::endl;
            std::cout << "  --grpc-port <port>         gRPC server port (default: 50051)" << std::endl;
            std::cout << "  --help                     Show this help message" << std::endl;
            return 0;
        }
    }

    // List available devices
    auto &deviceManager = medical::imaging::DeviceManager::getInstance();

    auto deviceIds = deviceManager.getAvailableDeviceIds();


    std::cout << "Available devices:" << std::endl;
    if (deviceIds.empty()) {
        std::cout << "  No devices found" << std::endl;
        return 1;
    }

    for (const auto &id: deviceIds) {
        auto device = deviceManager.getDevice(id);
        if (device) {
            std::cout << "  " << id << ": " << device->getDeviceName() << " (" << device->getDeviceModel() << ")" <<
                    std::endl;
        }
    }

    // If no device ID was specified, use the first one
    if (config.deviceId.empty() && !deviceIds.empty()) {
        config.deviceId = deviceIds[0];
        std::cout << "Using device: " << config.deviceId << std::endl;
    }

    // Initialize the service
    std::cout << "Initializing imaging service..." << std::endl;
    auto status = service.initialize(config);

    if (status != medical::imaging::ImagingService::Status::OK) {
        std::cerr << "Failed to initialize imaging service (error code: " << static_cast<int>(status) << ")" <<
                std::endl;
        return 1;
    }

    // Set a frame callback for logging
    service.setFrameCallback([](const std::shared_ptr<medical::imaging::Frame>& frame) {
        static uint64_t count = 0;
        static auto lastLogTime = std::chrono::steady_clock::now();

        count++;

        // Log stats every second
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastLogTime).count() >= 1) {
            std::cout << "Processed " << count << " frames, latest: "
                    << frame->getWidth() << "x" << frame->getHeight()
                    << " " << frame->getFormat() << std::endl;

            count = 0;
            lastLogTime = now;
        }
    });

    // Start the service
    std::cout << "Starting imaging service..." << std::endl;
    status = service.start();

    if (status != medical::imaging::ImagingService::Status::OK) {
        std::cerr << "Failed to start imaging service (error code: " << static_cast<int>(status) << ")" << std::endl;
        return 1;
    }

    std::cout << "Service running. Press Ctrl+C to stop." << std::endl;

    // Main loop
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Print statistics periodically
        auto stats = service.getStatistics();
        std::cout << "Statistics:" << std::endl;
        std::cout << "  Frame count: " << stats["frame_count"] << std::endl;
        std::cout << "  Dropped frames: " << stats["dropped_frames"] << std::endl;
        if (stats.count("average_fps")) {
            std::cout << "  FPS: " << stats["average_fps"] << std::endl;
        }
    }

    // Stop the service
    std::cout << "Stopping imaging service..." << std::endl;
    status = service.stop();

    if (status != medical::imaging::ImagingService::Status::OK) {
        std::cerr << "Failed to stop imaging service (error code: " << static_cast<int>(status) << ")" << std::endl;
        return 1;
    }

    std::cout << "Service stopped." << std::endl;

    return 0;
}
