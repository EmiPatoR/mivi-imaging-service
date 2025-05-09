#include "communication/grpc_server.h"
#include <grpcpp/grpcpp.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <thread>
#include <chrono>
#include <utility>

// This is a stub implementation without the service details
// We'll comment out the actual protobuf implementation
// until we properly set up the gRPC service interface


namespace medical::imaging {

// Implementation of gRPC service interface - stubbed out
class ImagingServiceImpl {
public:
    ImagingServiceImpl() = default;

    void setFrameProvider(const std::function<std::shared_ptr<Frame>()> &callback) {
        frameProviderCallback_ = callback;
    }

    void setDeviceControlHandler(
        const std::function<bool(const std::string&, const std::string&)> &callback) {
        deviceControlCallback_ = callback;
    }

private:
    std::function<std::shared_ptr<Frame>()> frameProviderCallback_;
    std::function<bool(const std::string&, const std::string&)> deviceControlCallback_;
};

GrpcServer::GrpcServer(std::string  address, int port)
    : address_(std::move(address)), port_(port), isRunning_(false) {

    // Create the service implementation
    service_ = std::make_unique<ImagingServiceImpl>();
}

GrpcServer::~GrpcServer() {
    // Stop the server if it's running
    if (isRunning_) {
        stop();
    }
}

GrpcServer::Status GrpcServer::start() {
    std::unique_lock<std::mutex> lock(mutex_);

    if (isRunning_) {
        return Status::ALREADY_RUNNING;
    }

    // Check address
    if (address_.empty()) {
        return Status::INVALID_ADDRESS;
    }

    // In a stub implementation, just mark as running
    isRunning_ = true;

    // Start a dummy thread
    serverThread_ = std::thread([this]() {
        while (isRunning_) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

    return Status::OK;
}

GrpcServer::Status GrpcServer::stop() {
    std::unique_lock<std::mutex> lock(mutex_);

    if (!isRunning_) {
        return Status::NOT_RUNNING;
    }

    // Mark as not running
    isRunning_ = false;

    // Wait for the thread to finish
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
    
    return Status::OK;
}

bool GrpcServer::isRunning() const {
    return isRunning_;
}

void GrpcServer::setFrameProvider(const std::function<std::shared_ptr<Frame>()>& callback) {
    if (service_) {
        service_->setFrameProvider(callback);
    }
    
    frameProviderCallback_ = callback;
}

void GrpcServer::setDeviceControlHandler(
    const std::function<bool(const std::string& command, const std::string& params)>& callback) {
    
    if (service_) {
        service_->setDeviceControlHandler(callback);
    }
    
    deviceControlCallback_ = callback;
}

std::string GrpcServer::getAddress() const {
    return address_;
}

int GrpcServer::getPort() const {
    return port_;
}

} // namespace medical::imaging
