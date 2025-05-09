#pragma once

#include <string>
#include <memory>
#include <functional>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <optional>
#include <map>

#include "frame/frame.h"

namespace medical::imaging {
    /**
     * @enum SharedMemoryType
     * @brief Types of shared memory implementations available
     */
    enum class SharedMemoryType {
        POSIX_SHM,         // POSIX shared memory (shm_open)
        SYSV_SHM,          // System V shared memory (shmget)
        MEMORY_MAPPED_FILE, // Memory-mapped file (best for cross-language)
        HUGE_PAGES         // Huge pages for better performance
    };

    /**
     * @class SharedMemory
     * @brief Provides zero-copy shared memory communication for frame data
     *
     * Enhanced implementation that supports multiple shared memory types and
     * is optimized for cross-language access with minimal overhead.
     */
    class SharedMemory {
    public:
        /**
         * @brief Status codes for shared memory operations
         */
        enum class Status {
            OK,                 // Operation completed successfully
            ALREADY_EXISTS,     // Shared memory region already exists
            CREATION_FAILED,    // Failed to create shared memory region
            NOT_INITIALIZED,    // Shared memory not initialized
            WRITE_FAILED,       // Failed to write to shared memory
            READ_FAILED,        // Failed to read from shared memory
            BUFFER_FULL,        // Ring buffer is full
            BUFFER_EMPTY,       // Ring buffer is empty
            INVALID_SIZE,       // Invalid size specified
            PERMISSION_DENIED,  // Permission denied
            TIMEOUT,            // Operation timed out
            INTERNAL_ERROR,     // Unspecified internal error
            NOT_SUPPORTED       // Operation not supported by this implementation
        };

        /**
         * @brief Frame header structure stored in shared memory
         */
        struct alignas(8) FrameHeader {
            uint64_t frameId;           // Unique frame identifier
            uint64_t timestamp;         // Frame timestamp (nanoseconds since epoch)
            uint32_t width;             // Frame width in pixels
            uint32_t height;            // Frame height in pixels
            uint32_t bytesPerPixel;     // Bytes per pixel
            uint32_t dataSize;          // Size of frame data in bytes
            uint32_t formatCode;        // Format identifier code
            uint32_t flags;             // Additional flags
            uint64_t sequenceNumber;    // Sequence number for ordering
            uint32_t metadataOffset;    // Offset to JSON metadata (if present)
            uint32_t metadataSize;      // Size of metadata in bytes
            uint64_t padding[4];        // Reserved for future use
        };

        /**
         * @brief Configuration for shared memory
         */
        struct Config {
            std::string name;             // Name of the shared memory region
            size_t size;                  // Size of the shared memory region in bytes
            SharedMemoryType type;        // Type of shared memory to use
            bool create;                  // Whether to create the region (server) or connect (client)
            size_t maxFrames;             // Maximum number of frames in the ring buffer
            bool useHugePages;            // Use huge pages for better performance
            bool lockInMemory;            // Prevent the memory from being swapped
            bool enableMetadata;          // Enable storing JSON metadata with frames
            std::string filePath;         // Path for memory-mapped file (if using file-backed)
            bool enableRealTimeThreads;   // Use real-time priority for notification threads
            bool dropFramesWhenFull;      // Whether to drop frames when buffer is full
            size_t maxFrameSize;          // Maximum size of a single frame in bytes

            // Constructor with sensible defaults
            Config() : name("ultrasound_frames"),
                       size(256 * 1024 * 1024), // 256 MB
                       type(SharedMemoryType::MEMORY_MAPPED_FILE), // Best for cross-language
                       create(false),
                       maxFrames(120),
                       useHugePages(false),
                       lockInMemory(true),
                       enableMetadata(true),
                       filePath("/dev/shm/ultrasound_frames"),
                       enableRealTimeThreads(true),
                       dropFramesWhenFull(true),
                       maxFrameSize(17 * 1024 * 1024) { // 17MB - enough for 4K frames
            }
        };

        /**
         * @brief Performance statistics for shared memory operations
         */
        struct Statistics {
            uint64_t totalFramesWritten;   // Total frames written
            uint64_t totalFramesRead;      // Total frames read
            uint64_t droppedFrames;        // Frames dropped due to buffer full
            uint64_t bufferFullCount;      // Number of buffer full events
            uint64_t writeLatencyNsAvg;    // Average write latency in nanoseconds
            uint64_t readLatencyNsAvg;     // Average read latency in nanoseconds
            uint64_t maxWriteLatencyNs;    // Maximum write latency observed
            uint64_t maxReadLatencyNs;     // Maximum read latency observed
            size_t peakMemoryUsage;        // Peak memory usage in bytes
            double averageFrameSize;       // Average frame size in bytes
        };

        /**
         * @brief Constructor
         * @param config Configuration for the shared memory
         */
        explicit SharedMemory(const Config &config);

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
        Status writeFrame(const std::shared_ptr<Frame> &frame);

        /**
         * @brief Write a frame to shared memory with timeout
         * @param frame Frame to write
         * @param timeoutMs Maximum time to wait in milliseconds
         * @return Status code indicating success or failure
         */
        Status writeFrameTimeout(const std::shared_ptr<Frame> &frame,
                                 unsigned int timeoutMs);

        /**
         * @brief Read the latest frame from shared memory (zero-copy)
         * @param frame Output parameter to store the read frame
         * @return Status code indicating success or failure
         */
        Status readLatestFrame(std::shared_ptr<Frame> &frame);

        /**
         * @brief Read the next frame from shared memory (zero-copy)
         * @param frame Output parameter to store the read frame
         * @param waitMilliseconds Maximum time to wait in milliseconds, 0 for non-blocking
         * @return Status code indicating success or failure
         */
        Status readNextFrame(std::shared_ptr<Frame> &frame,
                             unsigned int waitMilliseconds = 0);

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
         * @brief Set the thread affinity for notification threads
         * @param cpuCore CPU core to pin the thread to (-1 for no affinity)
         * @return Status code indicating success or failure
         */
        Status setThreadAffinity(int cpuCore);

        /**
         * @brief Set the thread priority for notification threads
         * @param priority Priority level (higher value = higher priority)
         * @return Status code indicating success or failure
         */
        Status setThreadPriority(int priority);

        /**
         * @brief Lock the shared memory in RAM (prevent swapping)
         * @return Status code indicating success or failure
         */
        Status lockMemory();

        /**
         * @brief Unlock the shared memory (allow swapping)
         * @return Status code indicating success or failure
         */
        Status unlockMemory();

        /**
         * @brief Get performance statistics
         * @return Statistics structure with performance metrics
         */
        Statistics getStatistics();

        /**
         * @brief Reset performance statistics
         */
        void resetStatistics();

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

        /**
         * @brief Get the type of shared memory implementation
         * @return SharedMemoryType enum value
         */
        SharedMemoryType getType() const;

        /**
         * @brief Get the maximum number of frames that can fit in the buffer
         * @return Maximum frame count
         */
        size_t getMaxFrames() const;

        /**
         * @brief Update the maximum frame size to handle larger frame formats
         * @param newMaxFrameSize New maximum frame size in bytes
         * @return Status code indicating success or failure
         */
        Status updateMaxFrameSize(size_t newMaxFrameSize);

        /**
         * @brief Get the current number of frames in the buffer
         * @return Current frame count
         */
        size_t getCurrentFrameCount() const;

        /**
         * @brief Check if the buffer is currently full
         * @return true if the buffer is full
         */
        bool isBufferFull() const;

        /**
         * @brief Check if the buffer is currently empty
         * @return true if the buffer is empty
         */
        bool isBufferEmpty() const;

        /**
         * @brief Update the shared memory metadata
         * @param key Metadata key
         * @param value Metadata value
         * @return Status code indicating success or failure
         */
        Status updateMetadata(const std::string& key, const std::string& value);

        /**
         * @brief Get a metadata value from the shared memory
         * @param key Metadata key
         * @return Metadata value, or empty string if not found
         */
        std::string getMetadata(const std::string& key) const;

    private:
        // Control block structure - stored at the beginning of shared memory
        struct alignas(64) ControlBlock {
            std::atomic<uint64_t> writeIndex;      // Current write position
            std::atomic<uint64_t> readIndex;       // Current read position
            std::atomic<uint64_t> frameCount;      // Number of frames in the buffer
            std::atomic<uint64_t> totalFramesWritten; // Total number of frames written
            std::atomic<uint64_t> totalFramesRead; // Total number of frames read
            std::atomic<uint64_t> droppedFrames;   // Frames dropped due to buffer full
            std::atomic<bool> active;              // Whether the shared memory is active
            std::atomic<uint64_t> lastWriteTime;   // Timestamp of last write (ns since epoch)
            std::atomic<uint64_t> lastReadTime;    // Timestamp of last read (ns since epoch)
            uint32_t metadataOffset;               // Offset to metadata area
            uint32_t metadataSize;                 // Size of metadata area
            std::atomic<uint32_t> flags;           // Additional flags
            uint8_t padding[184];                  // Padding to ensure proper alignment (full cache line)
        };

        // Private implementation to hide platform-specific details
        struct Impl;
        std::unique_ptr<Impl> impl_;

        // Configuration
        Config config_;

        // Initialization state
        std::atomic<bool> isInitialized_;

        // Callback related
        std::function<void(std::shared_ptr<Frame>)> frameCallback_;
        std::thread callbackThread_;
        std::atomic<bool> stopCallbackThread_;
        std::mutex callbackMutex_;

        // Statistics
        mutable std::mutex statsMutex_;
        Statistics stats_{};

        // Thread management
        int threadAffinity_;
        int threadPriority_;

        // Notification mechanism for new frames
        void notificationThread();

        // Helper method to calculate frame offset in the shared memory
        size_t calculateFrameOffset(uint64_t index) const;

        // Helper method to write JSON metadata to the shared memory
        bool writeMetadataJson(const std::string& json);

        // Helper method to read JSON metadata from the shared memory
        std::string readMetadataJson() const;

        // Helper method to convert frame to format code
        static uint32_t getFormatCode(const std::string& format);

        // Helper method to convert format code to string
        static std::string getFormatString(uint32_t formatCode);

        // Helper method to get current time in nanoseconds
        static uint64_t getCurrentTimeNanos();
    };

    // Helper class to manage multiple shared memory regions
    class SharedMemoryManager {
    public:
        static SharedMemoryManager &getInstance();

        std::shared_ptr<SharedMemory> createOrGet(const std::string &name,
                                                  size_t size = 0,
                                                  SharedMemoryType type = SharedMemoryType::MEMORY_MAPPED_FILE);

        bool releaseSharedMemory(const std::string &name);

        void releaseAll();

    private:
        SharedMemoryManager() = default;

        ~SharedMemoryManager();

        std::mutex mutex_;
        std::unordered_map<std::string, std::shared_ptr<SharedMemory>> sharedMemories_;
    };
} // namespace medical::imaging