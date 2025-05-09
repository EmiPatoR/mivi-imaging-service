#ifndef MEDICAL_IMAGING_FFI_BINDINGS_H
#define MEDICAL_IMAGING_FFI_BINDINGS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle to the imaging service
typedef void* UltrasoundServiceHandle;

// Status codes
#define ULTRASOUND_STATUS_OK                  0
#define ULTRASOUND_STATUS_INVALID_ARGUMENT    1
#define ULTRASOUND_STATUS_INVALID_HANDLE      2
#define ULTRASOUND_STATUS_DEVICE_ERROR        3
#define ULTRASOUND_STATUS_PROCESSING_ERROR    4
#define ULTRASOUND_STATUS_COMMUNICATION_ERROR 5
#define ULTRASOUND_STATUS_NOT_INITIALIZED     6
#define ULTRASOUND_STATUS_ALREADY_RUNNING     7
#define ULTRASOUND_STATUS_NOT_RUNNING         8
#define ULTRASOUND_STATUS_INTERNAL_ERROR      9
#define ULTRASOUND_STATUS_NOT_IMPLEMENTED     10

// Frame structure
typedef struct {
    uint64_t frame_id;              // Unique frame ID
    uint64_t timestamp_ns;          // Timestamp in nanoseconds
    int width;                      // Frame width in pixels
    int height;                     // Frame height in pixels
    int bytes_per_pixel;            // Number of bytes per pixel
    size_t data_size;               // Size of the data in bytes
    void* data;                     // Pointer to the frame data
    char format[32];                // Pixel format string
} UltrasoundFrame;

// Service configuration
typedef struct {
    const char* device_id;          // Device ID to use, NULL for auto-select
    int width;                      // Desired width
    int height;                     // Desired height
    double frame_rate;              // Desired frame rate
    const char* pixel_format;       // Desired pixel format
    int enable_audio;               // Whether to enable audio
    
    int enable_segmentation;        // Whether to enable segmentation
    int enable_calibration;         // Whether to enable calibration
    int processing_threads;         // Number of processing threads
    
    int enable_shared_memory;       // Whether to enable shared memory
    const char* shared_memory_name; // Name of the shared memory region
    size_t shared_memory_size;      // Size of the shared memory region
    
    int enable_grpc;                // Whether to enable gRPC
    const char* grpc_server_address;// gRPC server address
    int grpc_server_port;           // gRPC server port
    
    int frame_buffer_size;          // Frame buffer size
} UltrasoundServiceConfig;

// Device information
typedef struct {
    char device_id[128];            // Device ID
    char device_name[128];          // Device name
    char device_model[128];         // Device model
    int is_connected;               // Whether the device is connected
    int is_capturing;               // Whether the device is capturing
    int width;                      // Current width
    int height;                     // Current height
    double frame_rate;              // Current frame rate
    char pixel_format[32];          // Current pixel format
} UltrasoundDeviceInfo;

// Device configuration
typedef struct {
    int width;                      // Width
    int height;                     // Height
    double frame_rate;              // Frame rate
    char pixel_format[32];          // Pixel format
    int supports_audio;             // Whether audio is supported
} UltrasoundDeviceConfig;

// Statistics
typedef struct {
    uint64_t frame_count;           // Total frames captured
    uint64_t dropped_frames;        // Frames dropped
    double average_fps;             // Average frames per second
    double processing_time_ms;      // Average processing time in milliseconds
} UltrasoundStatistics;

// Callback function types
typedef void (*FrameCallbackFunc)(UltrasoundFrame* frame);
typedef void (*DeviceChangeCallbackFunc)(const char* device_id, int added);

// Create and destroy an imaging service
UltrasoundServiceHandle ultrasound_service_create();
void ultrasound_service_destroy(UltrasoundServiceHandle handle);

// Initialize and control an imaging service
int ultrasound_service_initialize(UltrasoundServiceHandle handle, const UltrasoundServiceConfig* config);
int ultrasound_service_start(UltrasoundServiceHandle handle);
int ultrasound_service_stop(UltrasoundServiceHandle handle);
int ultrasound_service_is_running(UltrasoundServiceHandle handle);

// Get the latest frame and statistics
int ultrasound_service_get_latest_frame(UltrasoundServiceHandle handle, UltrasoundFrame* frame);
int ultrasound_service_get_statistics(UltrasoundServiceHandle handle, UltrasoundStatistics* stats);

// Set callbacks
int ultrasound_service_set_frame_callback(UltrasoundServiceHandle handle, FrameCallbackFunc callback);

// Device management
int ultrasound_get_available_devices(char** device_ids, int max_devices);
int ultrasound_register_device_callback(DeviceChangeCallbackFunc callback);
int ultrasound_unregister_device_callback(int callback_id);
int ultrasound_get_device_info(const char* device_id, UltrasoundDeviceInfo* info);
int ultrasound_get_device_configurations(const char* device_id, UltrasoundDeviceConfig* configs, int max_configs);

// Version information
const char* ultrasound_get_version();

#ifdef __cplusplus
}
#endif

#endif // MEDICAL_IMAGING_FFI_BINDINGS_H