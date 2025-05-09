#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <chrono>
#include <functional>
#include <string>
#include <optional>
#include <variant>
#include <atomic>

namespace medical::imaging {
    /**
 * @enum BufferType
 * @brief Types of memory buffers for frames
 */
    enum class BufferType {
        CPU_MEMORY,        // Standard system memory
        GPU_MEMORY,        // GPU device memory (CUDA/OpenCL)
        DMA_BUFFER,        // Direct Memory Access buffer
        EXTERNAL_MEMORY    // Memory managed externally
    };

    /**
     * @struct FrameMetadata
     * @brief Enhanced metadata for medical imaging frames
     */
    struct FrameMetadata {
        uint64_t frameId = 0;                      // Unique frame ID
        uint64_t timestampNs = 0;                  // Capture timestamp in nanoseconds
        uint32_t width = 0;                        // Frame width in pixels
        uint32_t height = 0;                       // Frame height in pixels
        uint32_t bytesPerPixel = 0;                // Number of bytes per pixel
        std::string format;                        // Pixel format string

        // Acquisition metadata
        std::string deviceId;                      // ID of capturing device
        float exposureTimeMs = 0.0f;               // Exposure time in milliseconds
        uint32_t frameNumber = 0;                  // Sequential frame number

        // Processing status flags
        bool hasBeenProcessed = false;             // Whether frame has been processed
        bool hasCalibrationData = false;           // Whether frame has calibration data
        bool hasSegmentationData = false;          // Whether frame has segmentation data

        // For tracking/calibration
        std::vector<float> probePosition;          // 3D probe position [x,y,z]
        std::vector<float> probeOrientation;       // Quaternion [x,y,z,w]

        // Image quality and diagnostics
        float signalToNoiseRatio = 0.0f;           // SNR in dB
        float signalStrength = 0.0f;               // Signal strength (0.0-1.0)
        float confidenceScore = 0.0f;              // AI confidence (0.0-1.0)

        // Additional metadata as key-value pairs
        std::unordered_map<std::string, std::string> attributes;
    };

    /**
     * @class Frame
     * @brief Represents a captured ultrasound frame with enhanced zero-copy support
     *
     * This class encapsulates a single frame of ultrasound video data along
     * with relevant metadata. It provides enhanced support for zero-copy operations
     * and hardware acceleration.
     */
    class Frame {
    public:
        /**
         * @brief Create a new frame with allocated buffer
         * @param width Frame width in pixels
         * @param height Frame height in pixels
         * @param bytesPerPixel Number of bytes per pixel
         * @param format String identifier for the pixel format
         * @param bufferType Type of buffer to create
         * @return Shared pointer to the created frame
         */
        static std::shared_ptr<Frame> create(
            int width, int height, int bytesPerPixel, const std::string &format,
            BufferType bufferType = BufferType::CPU_MEMORY);

        /**
         * @brief Create a frame that wraps existing data (zero-copy)
         * @param data Pointer to the raw frame data
         * @param size Size of the data in bytes
         * @param width Frame width in pixels
         * @param height Frame height in pixels
         * @param bytesPerPixel Number of bytes per pixel
         * @param format String identifier for the pixel format
         * @param ownsData Whether this frame should take ownership of the data
         * @param bufferType Type of buffer being provided
         * @return Shared pointer to the created frame
         */
        static std::shared_ptr<Frame> createWithExternalData(
            void *data, size_t size, int width, int height, int bytesPerPixel,
            const std::string &format, bool ownsData = false,
            BufferType bufferType = BufferType::CPU_MEMORY);

        /**
         * @brief Create a frame that directly maps to shared memory
         * @param shmName Shared memory name
         * @param offset Offset within shared memory
         * @param size Size of the data in bytes
         * @param width Frame width in pixels
         * @param height Frame height in pixels
         * @param bytesPerPixel Number of bytes per pixel
         * @param format String identifier for the pixel format
         * @return Shared pointer to the created frame
         */
        static std::shared_ptr<Frame> createMapped(
            const std::string &shmName, size_t offset, size_t size,
            int width, int height, int bytesPerPixel, const std::string &format);

        /**
         * @brief Destructor
         */
        ~Frame();

        /**
         * @brief Set callback to be triggered when frame is destroyed
         * @param callback Function to call on destruction
         */
        void setOnDestroy(std::function<void()> callback) {
            onDestroyCallback_ = std::move(callback);
        }

        /**
         * @brief Get a pointer to the raw frame data
         * @return Pointer to the frame data
         */
        void *getData() const;

        /**
         * @brief Get the size of the frame data in bytes
         * @return Size in bytes
         */
        size_t getDataSize() const;

        /**
         * @brief Get the frame width in pixels
         * @return Width in pixels
         */
        int getWidth() const;

        /**
         * @brief Get the frame height in pixels
         * @return Height in pixels
         */
        int getHeight() const;

        /**
         * @brief Get the number of bytes per pixel
         * @return Bytes per pixel
         */
        int getBytesPerPixel() const;

        /**
         * @brief Get the pixel format string
         * @return Format identifier string
         */
        std::string getFormat() const;

        /**
         * @brief Get the frame timestamp
         * @return Timestamp when the frame was captured
         */
        std::chrono::system_clock::time_point getTimestamp() const;

        /**
         * @brief Set the frame timestamp
         * @param timestamp New timestamp value
         */
        void setTimestamp(std::chrono::system_clock::time_point timestamp);

        /**
         * @brief Get the frame unique ID
         * @return Frame ID
         */
        uint64_t getFrameId() const;

        /**
         * @brief Set the frame ID
         * @param id New frame ID
         */
        void setFrameId(uint64_t id);

        /**
         * @brief Get the buffer type
         * @return Buffer type enum
         */
        BufferType getBufferType() const;

        /**
         * @brief Get the enhanced metadata
         * @return Reference to frame metadata
         */
        const FrameMetadata &getMetadata() const;

        /**
         * @brief Get mutable reference to metadata for updates
         * @return Reference to frame metadata
         */
        FrameMetadata &getMetadataMutable();

        /**
         * @brief Check if this frame is using GPU memory
         * @return true if the frame is in GPU memory
         */
        bool isGpuMemory() const;

        /**
         * @brief Check if this frame is using DMA buffer
         * @return true if the frame is in a DMA buffer
         */
        bool isDmaBuffer() const;

        /**
         * @brief Check if this frame is directly mapped to shared memory
         * @return true if the frame is mapped to shared memory
         */
        bool isMappedToSharedMemory() const;

        /**
         * @brief Deep copy the frame to a new memory location
         * @param targetBufferType Type of buffer to create for the copy
         * @return New frame with copied data
         */
        std::shared_ptr<Frame> clone(BufferType targetBufferType = BufferType::CPU_MEMORY) const;

        /**
         * @brief Set legacy metadata associated with this frame (for backward compatibility)
         * @param key Metadata key
         * @param value Metadata value
         */
        void setMetadata(const std::string &key, const std::string &value);

        /**
         * @brief Get legacy metadata associated with this frame (for backward compatibility)
         * @param key Metadata key
         * @return Metadata value, or empty string if not found
         */
        std::string getMetadata(const std::string &key) const;

        /**
         * @brief Lock the frame data for CPU access
         * @param readOnly Whether access is read-only
         * @return true if lock was successful
         */
        bool lock(bool readOnly = true);

        /**
         * @brief Unlock the frame data
         */
        void unlock();

        /**
         * @brief Export frame to shared memory
         * @param shmName Name of shared memory region
         * @param offset Offset within shared memory
         * @return true if export was successful
         */
        bool exportToSharedMemory(const std::string &shmName, size_t offset);

    private:
        // Private implementation to hide details
        struct Impl;
        std::unique_ptr<Impl> impl_;
        std::function<void()> onDestroyCallback_;

        // Private constructor, use factory methods instead
        Frame();
    };
}
