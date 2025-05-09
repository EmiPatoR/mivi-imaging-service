#include "api/ffi_bindings.h"
#include "api/imaging_service.h"
#include "device/device_manager.h"
#include "frame/frame.h"
#include <cstring>
#include <iostream>
#include <vector>
#include <mutex>
#include <unordered_map>

// Basic implementation of the FFI bindings
// This is a stub implementation for building purposes

// Global context for callback management
struct CallbackContext {
    std::mutex mutex;
    std::unordered_map<int, medical::imaging::ImagingService*> services;
    std::unordered_map<int, FrameCallbackFunc> frameCallbacks;
    std::unordered_map<int, DeviceChangeCallbackFunc> deviceCallbacks;
    int nextId = 1;
};

static CallbackContext g_context;

// Helper function to convert C++ frame to C frame
void fillCFrame(const std::shared_ptr<medical::imaging::Frame>& cppFrame, UltrasoundFrame* cFrame) {
    if (!cppFrame || !cFrame) {
        return;
    }

    cFrame->frame_id = cppFrame->getFrameId();
    cFrame->width = cppFrame->getWidth();
    cFrame->height = cppFrame->getHeight();
    cFrame->bytes_per_pixel = cppFrame->getBytesPerPixel();
    cFrame->data_size = cppFrame->getDataSize();
    cFrame->data = cppFrame->getData();

    // Convert timestamp
    auto timestamp = cppFrame->getTimestamp();
    auto since_epoch = timestamp.time_since_epoch();
    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(since_epoch).count();
    cFrame->timestamp_ns = nanoseconds;

    // Copy format string
    const std::string& format = cppFrame->getFormat();
    strncpy(cFrame->format, format.c_str(), sizeof(cFrame->format) - 1);
    cFrame->format[sizeof(cFrame->format) - 1] = '\0';
}

// Frame callback adapter
void frameCallbackAdapter(int serviceId, const std::shared_ptr<medical::imaging::Frame>& frame) {
    std::unique_lock<std::mutex> lock(g_context.mutex);

    if (const auto it = g_context.frameCallbacks.find(serviceId); it != g_context.frameCallbacks.end() && it->second) {
        UltrasoundFrame cFrame = {};
        fillCFrame(frame, &cFrame);

        // Call the C callback
        it->second(&cFrame);
    }
}

// Device change callback adapter
void deviceChangeCallbackAdapter(int callbackId, const std::string& deviceId, bool added) {
    std::unique_lock<std::mutex> lock(g_context.mutex);

    auto it = g_context.deviceCallbacks.find(callbackId);
    if (it != g_context.deviceCallbacks.end() && it->second) {
        // Call the C callback
        it->second(deviceId.c_str(), added ? 1 : 0);
    }
}

extern "C" {

// Create a new imaging service
UltrasoundServiceHandle ultrasound_service_create() {
    auto* service = new medical::imaging::ImagingService();
    if (!service) {
        return nullptr;
    }

    std::unique_lock<std::mutex> lock(g_context.mutex);
    const int id = g_context.nextId++;
    g_context.services[id] = service;

    return reinterpret_cast<UltrasoundServiceHandle>(static_cast<uintptr_t>(id));
}

// Destroy an imaging service
void ultrasound_service_destroy(UltrasoundServiceHandle handle) {
    if (!handle) {
        return;
    }

    int id = static_cast<int>(reinterpret_cast<uintptr_t>(handle));

    std::unique_lock<std::mutex> lock(g_context.mutex);

    auto it = g_context.services.find(id);
    if (it != g_context.services.end()) {
        delete it->second;
        g_context.services.erase(it);
    }

    // Remove any callbacks
    g_context.frameCallbacks.erase(id);
}

// Initialize an imaging service
int ultrasound_service_initialize(UltrasoundServiceHandle handle, const UltrasoundServiceConfig* config) {
    return ULTRASOUND_STATUS_OK; // Stub implementation
}

// Start an imaging service
int ultrasound_service_start(UltrasoundServiceHandle handle) {
    return ULTRASOUND_STATUS_OK; // Stub implementation
}

// Stop an imaging service
int ultrasound_service_stop(UltrasoundServiceHandle handle) {
    return ULTRASOUND_STATUS_OK; // Stub implementation
}

// Check if an imaging service is running
int ultrasound_service_is_running(UltrasoundServiceHandle handle) {
    return 0; // Stub implementation
}

// Set a frame callback
int ultrasound_service_set_frame_callback(UltrasoundServiceHandle handle, FrameCallbackFunc callback) {
    return ULTRASOUND_STATUS_OK; // Stub implementation
}

// Get available devices
int ultrasound_get_available_devices(char** device_ids, int max_devices) {
    return 0; // Stub implementation
}

// Register a device change callback
int ultrasound_register_device_callback(DeviceChangeCallbackFunc callback) {
    return 0; // Stub implementation
}

// Unregister a device change callback
int ultrasound_unregister_device_callback(int callback_id) {
    return 0; // Stub implementation
}

// Get device information
int ultrasound_get_device_info(const char* device_id, UltrasoundDeviceInfo* info) {
    return ULTRASOUND_STATUS_OK; // Stub implementation
}

// Get supported device configurations
int ultrasound_get_device_configurations(const char* device_id, UltrasoundDeviceConfig* configs, int max_configs) {
    return 0; // Stub implementation
}

// Get statistics
int ultrasound_service_get_statistics(UltrasoundServiceHandle handle, UltrasoundStatistics* stats) {
    return ULTRASOUND_STATUS_OK; // Stub implementation
}

// Get the latest frame
int ultrasound_service_get_latest_frame(UltrasoundServiceHandle handle, UltrasoundFrame* frame) {
    return ULTRASOUND_STATUS_NOT_IMPLEMENTED; // Stub implementation
}

// Version information
const char* ultrasound_get_version() {
    return "Ultrasound Imaging SDK v1.0.0";
}

} // extern "C"