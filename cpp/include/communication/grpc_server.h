#pragma once

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>

#include "frame/frame.h"

namespace medical {
namespace imaging {

// Forward declaration - implementation is in grpc_server.cpp
class ImagingServiceImpl;

/**
 * @class GrpcServer
 * @brief Implements a gRPC server for remote access to imaging data
 *
 * This class provides a gRPC interface for remote services to access
 * ultrasound imaging data and control the imaging service.
 */
class GrpcServer {
public:
    /**
     * @brief Status codes for gRPC server operations
     */
    enum class Status {
        OK,                  // Operation completed successfully
        ALREADY_RUNNING,     // Server is already running
        START_FAILED,        // Failed to start server
        NOT_RUNNING,         // Server is not running
        INVALID_ADDRESS,     // Invalid address specified
        INTERNAL_ERROR       // Unspecified internal error
    };

    /**
     * @brief Constructor
     * @param address Server address to bind to (e.g., "0.0.0.0")
     * @param port Server port to bind to
     */
    GrpcServer(std::string  address, int port);

    /**
     * @brief Destructor
     */
    ~GrpcServer();

    /**
     * @brief Start the gRPC server
     * @return Status code indicating success or failure
     */
    Status start();

    /**
     * @brief Stop the gRPC server
     * @return Status code indicating success or failure
     */
    Status stop();

    /**
     * @brief Check if the server is running
     * @return true if running, false otherwise
     */
    bool isRunning() const;

    /**
     * @brief Set the callback for providing the latest frame
     * @param callback Function that returns the latest frame
     */
    void setFrameProvider(const std::function<std::shared_ptr<Frame>()>& callback);

    /**
     * @brief Set the callback for device control
     * @param callback Function that handles device control commands
     */
    void setDeviceControlHandler(
        const std::function<bool(const std::string& command, const std::string& params)>& callback);

    /**
     * @brief Get the server address
     * @return Server address
     */
    std::string getAddress() const;

    /**
     * @brief Get the server port
     * @return Server port
     */
    int getPort() const;

private:
    // Server implementation details
    std::string address_;
    int port_;
    std::atomic<bool> isRunning_;

    // Service implementation (forward declared)
    std::unique_ptr<ImagingServiceImpl> service_;

    // Thread for running the server
    std::thread serverThread_;
    std::mutex mutex_;

    // Callbacks
    std::function<std::shared_ptr<Frame>()> frameProviderCallback_;
    std::function<bool(const std::string&, const std::string&)> deviceControlCallback_;
};

} // namespace imaging
} // namespace medical