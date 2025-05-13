#include "api/imaging_service.h"
#include <iostream>
#include <string>
#include <csignal>
#include <thread>
#include <atomic>
#include <iomanip>
#include <chrono>
#include <fstream>
#include <sys/resource.h>

// Global variables
std::atomic<bool> g_running(true);

// Signal handler
void signalHandler(int signal) {
    std::cout << "Received signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
}

void printBanner() {
    std::cout << "\n";
    std::cout << "┌─────────────────────────────────────────────────────────┐\n";
    std::cout << "│                                                         │\n";
    std::cout << "│   Medical Ultrasound Imaging Acquisition Service        │\n";
    std::cout << "│                                                         │\n";
    std::cout << "│   Zero-Copy Frame Acquisition with Shared Memory        │\n";
    std::cout << "│                                                         │\n";
    std::cout << "└─────────────────────────────────────────────────────────┘\n";
    std::cout << "\n";
}

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]\n";
    std::cout << "Options:\n";
    std::cout << "  --device <id>              Device ID to use (default: auto-select)\n";
    std::cout << "  --width <pixels>           Frame width (default: 1920)\n";
    std::cout << "  --height <pixels>          Frame height (default: 1080)\n";
    std::cout << "  --frame-rate <fps>         Frame rate (default: 60.0)\n";
    std::cout << "  --pixel-format <format>    Pixel format (default: YUV)\n";
    std::cout << "  --no-direct-memory         Disable direct memory access\n";
    std::cout << "  --no-realtime              Disable realtime priority\n";
    std::cout << "  --thread-affinity <cpu>    Set thread affinity to CPU core\n";
    std::cout << "  --no-pin-memory            Don't pin memory (allow swapping)\n";
    std::cout << "  --no-shared-memory         Disable shared memory\n";
    std::cout << "  --shared-memory-name <name> Shared memory name (default: ultrasound_frames)\n";
    std::cout << "  --shared-memory-size <bytes> Shared memory size (default: 128MB)\n";
    std::cout << "  --shared-memory-type <type> Shared memory type (0=POSIX, 1=SYSV, 2=MMF, 3=HUGE)\n";
    std::cout << "  --buffer-size <frames>     Frame buffer size (default: 120)\n";
    std::cout << "  --no-drop-frames           Don't drop frames when buffer is full\n";
    std::cout << "  --enable-logging           Enable performance logging\n";
    std::cout << "  --log-interval <ms>        Log interval in ms (default: 5000)\n";
    std::cout << "  --diagnostics-file <path>  Path to write diagnostics (default: none)\n";
    std::cout << "  --nice-value <value>       Process nice value (-20 to 19, default: -10)\n";
    std::cout << "  --help                     Show this help message\n";
}

void printStatistics(const std::map<std::string, std::string>& stats) {
    // Clear screen
    std::cout << "\033[2J\033[1;1H";

    std::cout << "┌─────────────────────────────────────────────────────────┐\n";
    std::cout << "│ Acquisition Service Statistics                          │\n";
    std::cout << "├─────────────────────────────────────────────────────────┤\n";

    // Performance metrics
    if (stats.count("frame_count")) {
        std::cout << "│ Frame count: " << std::setw(45) << std::left
                  << stats.at("frame_count") << "│\n";
    }

    if (stats.count("dropped_frames")) {
        std::cout << "│ Dropped frames: " << std::setw(42) << std::left
                  << stats.at("dropped_frames") << "│\n";
    }

    if (stats.count("average_fps")) {
        std::cout << "│ Average FPS: " << std::setw(44) << std::left
                  << stats.at("average_fps") << "│\n";
    }

    if (stats.count("current_fps")) {
        std::cout << "│ Current FPS: " << std::setw(44) << std::left
                  << stats.at("current_fps") << "│\n";
    }

    if (stats.count("average_latency_ms")) {
        std::cout << "│ Average latency (ms): " << std::setw(37) << std::left
                  << stats.at("average_latency_ms") << "│\n";
    }

    if (stats.count("max_latency_ms")) {
        std::cout << "│ Max latency (ms): " << std::setw(40) << std::left
                  << stats.at("max_latency_ms") << "│\n";
    }

    if (stats.count("cpu_usage_percent")) {
        std::cout << "│ CPU usage (%): " << std::setw(42) << std::left
                  << stats.at("cpu_usage_percent") << "│\n";
    }

    if (stats.count("memory_usage_mb")) {
        std::cout << "│ Memory usage (MB): " << std::setw(39) << std::left
                  << stats.at("memory_usage_mb") << "│\n";
    }

    // Shared memory stats
    if (stats.count("shm_frames_written")) {
        std::cout << "├─────────────────────────────────────────────────────────┤\n";
        std::cout << "│ Shared Memory Statistics                                │\n";
        std::cout << "├─────────────────────────────────────────────────────────┤\n";

        std::cout << "│ Frames written to SHM: " << std::setw(36) << std::left
                  << stats.at("shm_frames_written") << "│\n";

        std::cout << "│ Frames read from SHM: " << std::setw(37) << std::left
                  << stats.at("shm_frames_read") << "│\n";

        std::cout << "│ Frames dropped in SHM: " << std::setw(36) << std::left
                  << stats.at("shm_dropped_frames") << "│\n";

        std::cout << "│ Write latency (ns): " << std::setw(39) << std::left
                  << stats.at("shm_avg_write_latency_ns") << "│\n";

        std::cout << "│ Read latency (ns): " << std::setw(40) << std::left
                  << stats.at("shm_avg_read_latency_ns") << "│\n";

        std::cout << "│ Current buffer usage: " << std::setw(37) << std::left
                  << stats.at("shm_current_frame_count") << "│\n";

        std::cout << "│ Buffer full: " << std::setw(44) << std::left
                  << stats.at("shm_is_buffer_full") << "│\n";
    }

    std::cout << "└─────────────────────────────────────────────────────────┘\n";
}

void printDevices(const std::vector<std::string>& deviceIds,
                  medical::imaging::DeviceManager& deviceManager) {
    std::cout << "┌─────────────────────────────────────────────────────────┐\n";
    std::cout << "│ Available Devices                                       │\n";
    std::cout << "├─────────────────────────────────────────────────────────┤\n";

    if (deviceIds.empty()) {
        std::cout << "│ No devices found                                        │\n";
    } else {
        for (const auto& id : deviceIds) {
            auto device = deviceManager.getDevice(id);
            if (device) {
                std::string deviceInfo = " " + id + ": " + device->getDeviceName() +
                                       " (" + device->getDeviceModel() + ")";

                // Truncate if too long
                if (deviceInfo.length() > 55) {
                    deviceInfo = deviceInfo.substr(0, 52) + "...";
                }

                // Pad to fill the width
                deviceInfo.resize(55, ' ');

                std::cout << "│" << deviceInfo << "│\n";
            }
        }
    }

    std::cout << "└─────────────────────────────────────────────────────────┘\n\n";
}

// New function to set process scheduling priority
bool setProcessPriority(int niceValue) {
    // Set nice value (process priority)
    int result = setpriority(PRIO_PROCESS, 0, niceValue);
    if (result != 0) {
        std::cerr << "Failed to set nice value: " << errno << std::endl;
        return false;
    }

    std::cout << "Process priority set to nice value: " << niceValue << std::endl;
    return true;
}

// New function to write diagnostics to a file periodically
void writeDiagnostics(const std::string& filePath,
                     const medical::imaging::ImagingService& service) {
    if (filePath.empty()) {
        return;
    }

    static auto lastWriteTime = std::chrono::system_clock::now();
    auto now = std::chrono::system_clock::now();

    // Write every 30 seconds
    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastWriteTime).count() >= 30) {
        service.dumpDiagnostics(filePath);
        lastWriteTime = now;
    }
}

int main(int argc, char *argv[]) {
    // Register signal handler
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    printBanner();

    // Default nice value
    int niceValue = -10;

    // Diagnostics file path
    std::string diagnosticsFile;

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
        } else if (arg == "--no-direct-memory") {
            config.enableDirectMemoryAccess = false;
        } else if (arg == "--no-realtime") {
            config.useRealtimePriority = false;
        } else if (arg == "--thread-affinity" && i + 1 < argc) {
            config.threadAffinity = std::stoi(argv[++i]);
        } else if (arg == "--no-pin-memory") {
            config.pinMemory = false;
        } else if (arg == "--no-shared-memory") {
            config.enableSharedMemory = false;
        } else if (arg == "--shared-memory-name" && i + 1 < argc) {
            config.sharedMemoryName = argv[++i];
        } else if (arg == "--shared-memory-size" && i + 1 < argc) {
            config.sharedMemorySize = std::stoull(argv[++i]);
        } else if (arg == "--shared-memory-type" && i + 1 < argc) {
            int type = std::stoi(argv[++i]);
            switch (type) {
                case 0: config.sharedMemoryType = medical::imaging::SharedMemoryType::POSIX_SHM; break;
                case 1: config.sharedMemoryType = medical::imaging::SharedMemoryType::SYSV_SHM; break;
                case 2: config.sharedMemoryType = medical::imaging::SharedMemoryType::MEMORY_MAPPED_FILE; break;
                case 3: config.sharedMemoryType = medical::imaging::SharedMemoryType::HUGE_PAGES; break;
                default:
                    std::cerr << "Invalid shared memory type: " << type << std::endl;
                    return 1;
            }
        } else if (arg == "--buffer-size" && i + 1 < argc) {
            config.frameBufferSize = std::stoi(argv[++i]);
        } else if (arg == "--no-drop-frames") {
            config.dropFramesWhenFull = false;
        } else if (arg == "--enable-logging") {
            config.logPerformanceStats = true;
        } else if (arg == "--log-interval" && i + 1 < argc) {
            config.performanceLogIntervalMs = std::stoi(argv[++i]);
        } else if (arg == "--diagnostics-file" && i + 1 < argc) {
            diagnosticsFile = argv[++i];
        } else if (arg == "--nice-value" && i + 1 < argc) {
            niceValue = std::stoi(argv[++i]);
            // Clamp to valid range
            niceValue = std::max(-20, std::min(19, niceValue));
        } else if (arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    config.deviceConfig.bufferCount = 16; // Increase from 3 to 6
    config.sharedMemorySize = 512 * 1024 * 1024; // 512 MB instead of 128 MB
    //config.maxFrameSize = 17 * 1024 * 1024; // 17MB per frame

    // Set process priority
    if (!setProcessPriority(niceValue)) {
        std::cerr << "Failed to set nice value: " << niceValue << std::endl;
    }

    // List available devices
    auto &deviceManager = medical::imaging::DeviceManager::getInstance();
    auto deviceIds = deviceManager.getAvailableDeviceIds();

    printDevices(deviceIds, deviceManager);

    // If no device ID was specified, use the first one
    if (config.deviceId.empty() && !deviceIds.empty()) {
        config.deviceId = deviceIds[0];
        std::cout << "Using device: " << config.deviceId << std::endl;
    }

    // Initialize the service
    std::cout << "Initializing imaging service..." << std::endl;
    auto status = service.initialize(config);

    if (status != medical::imaging::ImagingService::Status::OK) {
        std::cerr << "Failed to initialize imaging service (error code: " << static_cast<int>(status) << ")" << std::endl;
        return 1;
    }

    // Start the service
    std::cout << "Starting imaging service..." << std::endl;
    status = service.start();

    if (status != medical::imaging::ImagingService::Status::OK) {
        std::cerr << "Failed to start imaging service (error code: " << static_cast<int>(status) << ")" << std::endl;
        return 1;
    }

    std::cout << "Service running. Press Ctrl+C to stop." << std::endl;
    std::cout << std::endl;
    std::cout << "Frames are being written to shared memory: " << config.sharedMemoryName << std::endl;
    std::cout << "Other services can now connect to this shared memory to process frames." << std::endl;
    std::cout << std::endl;

    // Create a file to signal that the service is ready
    if (!diagnosticsFile.empty()) {
        std::ofstream readyFile("/tmp/imaging_service_ready");
        if (readyFile.is_open()) {
            readyFile << "ready" << std::endl;
            readyFile.close();
        }
    }

    // Main loop
    while (g_running) {
        // Sleep briefly
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Print statistics
        auto stats = service.getStatistics();
        printStatistics(stats);

        // Write diagnostics if enabled
        if (!diagnosticsFile.empty()) {
            writeDiagnostics(diagnosticsFile, service);
        }
    }

    // Stop the service
    std::cout << "Stopping imaging service..." << std::endl;
    status = service.stop();

    if (status != medical::imaging::ImagingService::Status::OK) {
        std::cerr << "Failed to stop imaging service (error code: " << static_cast<int>(status) << ")" << std::endl;
        return 1;
    }

    // Remove ready file if it exists
    if (!diagnosticsFile.empty()) {
        std::remove("/tmp/imaging_service_ready");
    }

    std::cout << "Service stopped." << std::endl;
    return 0;
}