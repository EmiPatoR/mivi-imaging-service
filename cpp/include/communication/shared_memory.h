#pragma once

#include <string>
#include <memory>
#include <functional>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>

#include "frame/frame.h"

namespace medical {
    namespace imaging {
        /**
         * @class SharedMemory
         * @brief Provides zero-copy shared memory communication for frame data
         *
         * This class implements a shared memory ring buffer to allow efficient
         * zero-copy sharing of frame data between processes on the same machine.
         * It's optimized for high-frequency, high-volume data transfer with minimal latency.
         */
        class SharedMemory {
        public:
            /**
             * @brief Status codes for shared memory operations
             */
            enum class Status {
                OK, // Operation completed successfully
                ALREADY_EXISTS, // Shared memory region already exists
                CREATION_FAILED, // Failed to create shared memory region
                NOT_INITIALIZED, // Shared memory not initialized
                WRITE_FAILED, // Failed to write to shared memory
                READ_FAILED, // Failed to read from shared memory
                BUFFER_FULL, // Ring buffer is full
                BUFFER_EMPTY, // Ring buffer is empty
                INVALID_SIZE, // Invalid size specified
                PERMISSION_DENIED, // Permission denied
                TIMEOUT, // Operation timed out
                INTERNAL_ERROR // Unspecified internal error
            };

            /**
             * @brief Frame header structure stored in shared memory
             */
            struct FrameHeader {
                uint64_t frameId; // Unique frame identifier
                uint64_t timestamp; // Frame timestamp (nanoseconds since epoch)
                uint32_t width; // Frame width in pixels
                uint32_t height; // Frame height in pixels
                uint32_t bytesPerPixel; // Bytes per pixel
                uint32_t dataSize; // Size of frame data in bytes
                uint32_t formatCode; // Format identifier code
                uint32_t flags; // Additional flags
                uint64_t sequenceNumber; // Sequence number for ordering
                uint64_t padding[2]; // Reserved for future use
            };

            /**
             * @brief Constructor for creating a new shared memory region
             * @param name Name of the shared memory region
             * @param size Size of the shared memory region in bytes
             * @param create Whether to create the region (server) or connect to existing (client)
             */
            SharedMemory(std::string name, size_t size, bool create);

            /**
             * @brief Destructor
             */
            ~SharedMemory();

            /**
             * @brief Initialize the shared memory region
             * @return Status code indicating success or failure
             */
            Status initialize();

            /**
             * @brief Check if the shared memory is initialized
             * @return true if initialized, false otherwise
             */
            bool isInitialized() const;

            /**
             * @brief Write a frame to shared memory (zero-copy when possible)
             * @param frame Frame to write
             * @return Status code indicating success or failure
             */
            Status writeFrame(const std::shared_ptr<Frame>& frame) const;

            /**
             * @brief Read the latest frame from shared memory (zero-copy)
             * @param frame Output parameter to store the read frame
             * @return Status code indicating success or failure
             */
            Status readLatestFrame(std::shared_ptr<Frame> &frame) const;

            /**
             * @brief Read the next frame from shared memory (zero-copy)
             * @param frame Output parameter to store the read frame
             * @param waitMilliseconds Maximum time to wait in milliseconds, 0 for non-blocking
             * @return Status code indicating success or failure
             */
            Status readNextFrame(std::shared_ptr<Frame> &frame, unsigned int waitMilliseconds = 0) const;

            /**
             * @brief Register a callback for new frames
             * @param callback Function to call when a new frame is available
             * @return Status code indicating success or failure
             */
            Status registerFrameCallback(std::function<void(std::shared_ptr<Frame>)> callback);

            /**
             * @brief Unregister the frame callback
             * @return Status code indicating success or failure
             */
            Status unregisterFrameCallback();

            /**
             * @brief Get the name of the shared memory region
             * @return Name of the shared memory region
             */
            std::string getName() const;

            /**
             * @brief Get the size of the shared memory region
             * @return Size in bytes
             */
            size_t getSize() const;

        private:
            // Private implementation to hide platform-specific details
            struct Impl;
            std::unique_ptr<Impl> impl_;

            // Platform-independent members
            std::string name_;
            size_t size_;
            bool isServer_; // Whether this is the server (creator) or client
            std::atomic<bool> isInitialized_;

            // Callback related
            std::function<void(std::shared_ptr<Frame>)> frameCallback_;
            std::thread callbackThread_;
            std::atomic<bool> stopCallbackThread_;
            std::mutex callbackMutex_;

            // Notification mechanism for new frames
            void notificationThread();
        };
    } // namespace imaging
} // namespace medicalh
