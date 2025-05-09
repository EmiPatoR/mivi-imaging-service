#include "communication/shared_memory.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <thread>
#include <utility>


    namespace medical::imaging {
        /**
         * @brief Platform-specific implementation for POSIX shared memory
         */
        struct SharedMemory::Impl {
            int fd; // Shared memory file descriptor
            void *mapping; // Memory mapping pointer
            std::string name; // Shared memory name
            size_t size{}; // Total size of shared memory region
            bool isServer{}; // Whether this is the server (creator)

            // Ring buffer control structures
            struct ControlBlock {
                std::atomic<uint64_t> writeIndex; // Current write position
                std::atomic<uint64_t> readIndex; // Current read position
                std::atomic<uint64_t> frameCount; // Number of frames in the buffer
                std::atomic<uint64_t> totalFrames; // Total number of frames written
                std::atomic<uint64_t> droppedFrames; // Frames dropped due to buffer full
                std::atomic<bool> active; // Whether the shared memory is active
            };

            ControlBlock *controlBlock; // Pointer to control block in shared memory
            size_t controlBlockSize; // Size of the control block
            size_t dataOffset; // Offset to the start of the data region
            size_t maxFrames; // Maximum number of frames in the ring buffer

            // Constructor
            Impl() : fd(-1), mapping(nullptr), controlBlock(nullptr), controlBlockSize(0),
                     dataOffset(0), maxFrames(0) {
            }

            // Destructor
            ~Impl() {
                if (mapping) {
                    munmap(mapping, size);
                    mapping = nullptr;
                }

                if (fd >= 0) {
                    close(fd);
                    fd = -1;
                }

                // If this is the server, unlink the shared memory
                if (isServer) {
                    shm_unlink(name.c_str());
                }
            }

            // Initialize the shared memory
            Status initialize(const std::string &shmName, const size_t shmSize, const bool create) {
                name = "/" + shmName; // POSIX shared memory names must start with '/'
                size = shmSize;
                isServer = create;

                // Calculate control block size and data offset
                controlBlockSize = sizeof(ControlBlock);
                dataOffset = controlBlockSize;

                // Size must be large enough for the control block and at least one frame
                if (size <= dataOffset + sizeof(FrameHeader)) {
                    return Status::INVALID_SIZE;
                }

                if (create) {
                    // Create the shared memory as the server
                    fd = shm_open(name.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
                    if (fd < 0) {
                        return SharedMemory::Status::CREATION_FAILED;
                    }

                    // Set the size
                    if (ftruncate(fd, size) < 0) {
                        close(fd);
                        fd = -1;
                        shm_unlink(name.c_str());
                        return SharedMemory::Status::CREATION_FAILED;
                    }
                } else {
                    // Open existing shared memory as a client
                    fd = shm_open(name.c_str(), O_RDWR, S_IRUSR | S_IWUSR);
                    if (fd < 0) {
                        return SharedMemory::Status::CREATION_FAILED;
                    }

                    // Get the actual size
                    struct stat sb{};
                    if (fstat(fd, &sb) < 0) {
                        close(fd);
                        fd = -1;
                        return SharedMemory::Status::CREATION_FAILED;
                    }

                    size = sb.st_size;
                }

                // Map the shared memory into our address space
                mapping = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                if (mapping == MAP_FAILED) {
                    mapping = nullptr;
                    close(fd);
                    fd = -1;
                    if (create) {
                        shm_unlink(name.c_str());
                    }
                    return SharedMemory::Status::NOT_INITIALIZED;
                }

                // Set up the control block
                controlBlock = static_cast<ControlBlock *>(mapping);

                if (create) {
                    // Initialize the control block as server
                    new(controlBlock) ControlBlock();
                    controlBlock->writeIndex = 0;
                    controlBlock->readIndex = 0;
                    controlBlock->frameCount = 0;
                    controlBlock->totalFrames = 0;
                    controlBlock->droppedFrames = 0;
                    controlBlock->active = true;

                    // Calculate the maximum number of frames that can fit
                    // This is a rough estimate assuming average frame size
                    // In practice, frames can be different sizes
                    size_t avgFrameSize = 1920 * 1080 * 2 + sizeof(FrameHeader); // Assume 1080p YUV
                    maxFrames = (size - dataOffset) / avgFrameSize;
                    if (maxFrames < 1) {
                        maxFrames = 1;
                    }
                } else {
                    // Client just uses the existing control block
                    // Wait for it to be initialized
                    int attempts = 0;
                    while (!controlBlock->active && attempts < 100) {
                        usleep(10000); // 10ms
                        attempts++;
                    }

                    if (!controlBlock->active) {
                        munmap(mapping, size);
                        mapping = nullptr;
                        close(fd);
                        fd = -1;
                        return SharedMemory::Status::INTERNAL_ERROR;
                    }
                }

                return SharedMemory::Status::OK;
            }

            // Get the offset of a frame in the ring buffer
            [[nodiscard]] size_t getFrameOffset(const uint64_t index) const {
                return dataOffset + (index % maxFrames) * sizeof(FrameHeader);
            }

            // Get a pointer to a frame header
            [[nodiscard]] FrameHeader *getFrameHeader(const uint64_t index) const {
                const size_t offset = getFrameOffset(index);
                if (offset + sizeof(FrameHeader) > size) {
                    return nullptr;
                }

                return reinterpret_cast<FrameHeader *>(static_cast<uint8_t *>(mapping) + offset);
            }

            // Get a pointer to frame data
            [[nodiscard]] void *getFrameData(const uint64_t index, const size_t headerSize) const {
                const size_t offset = getFrameOffset(index) + headerSize;
                if (offset >= size) {
                    return nullptr;
                }

                return static_cast<uint8_t *>(mapping) + offset;
            }
        };

        SharedMemory::SharedMemory(std::string name, const size_t size, const bool create)
            : impl_(std::make_unique<Impl>()),
              name_(std::move(name)),
              size_(size),
              isServer_(create),
              isInitialized_(false),
              stopCallbackThread_(false) {
        }

        SharedMemory::~SharedMemory() {
            // Stop the notification thread if it's running
            if (callbackThread_.joinable()) {
                stopCallbackThread_ = true;
                callbackThread_.join();
            }
        }

        SharedMemory::Status SharedMemory::initialize() {
            if (isInitialized_) {
                return Status::ALREADY_EXISTS;
            }

            // Initialize the platform-specific implementation
            Status status = impl_->initialize(name_, size_, isServer_);
            if (status != Status::OK) {
                return status;
            }

            isInitialized_ = true;

            // Start the notification thread if we're a client
            if (!isServer_ && frameCallback_) {
                stopCallbackThread_ = false;
                callbackThread_ = std::thread(&SharedMemory::notificationThread, this);
            }

            return Status::OK;
        }

        bool SharedMemory::isInitialized() const {
            return isInitialized_;
        }

        SharedMemory::Status SharedMemory::writeFrame(const std::shared_ptr<Frame>& frame) const {
            if (!isInitialized_ || !impl_->controlBlock) {
                return Status::NOT_INITIALIZED;
            }

            if (!frame || !frame->getData() || frame->getDataSize() == 0) {
                return Status::INVALID_SIZE;
            }

            // Check if we have room in the ring buffer
            if (impl_->controlBlock->frameCount >= impl_->maxFrames) {
                // Buffer is full, oldest frame will be overwritten
                impl_->controlBlock->droppedFrames++;
            }

            // Get the current write index
            uint64_t writeIndex = impl_->controlBlock->writeIndex;

            // Get the frame header
            FrameHeader *header = impl_->getFrameHeader(writeIndex);
            if (!header) {
                return Status::INTERNAL_ERROR;
            }

            // Calculate the space required for this frame
            const size_t requiredSize = sizeof(FrameHeader) + frame->getDataSize();

            // Check if there's enough space for this frame
            if (const size_t frameOffset = impl_->getFrameOffset(writeIndex); frameOffset + requiredSize > size_) {
                return Status::BUFFER_FULL;
            }

            // Fill the header
            header->frameId = frame->getFrameId();
            auto timestamp = frame->getTimestamp();
            auto since_epoch = timestamp.time_since_epoch();
            auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(since_epoch).count();
            header->timestamp = static_cast<uint64_t>(nanoseconds);
            header->width = frame->getWidth();
            header->height = frame->getHeight();
            header->bytesPerPixel = frame->getBytesPerPixel();
            header->dataSize = frame->getDataSize();
            header->sequenceNumber = writeIndex;

            // Set format code
            const std::string &format = frame->getFormat();
            uint32_t formatCode = 0;
            if (format == "YUV" || format == "YUV422") {
                formatCode = 0x01;
            } else if (format == "RGB" || format == "BGRA") {
                formatCode = 0x02;
            } else if (format == "YUV10" || format == "YUV422_10") {
                formatCode = 0x03;
            } else if (format == "RGB10") {
                formatCode = 0x04;
            } else {
                formatCode = 0xFF; // Unknown
            }
            header->formatCode = formatCode;
            header->flags = 0;

            // Copy the frame data
            void *dataPtr = impl_->getFrameData(writeIndex, sizeof(FrameHeader));
            if (!dataPtr) {
                return Status::INTERNAL_ERROR;
            }

            // Copy the frame data
            std::memcpy(dataPtr, frame->getData(), frame->getDataSize());

            // Update the write index and frame count
            uint64_t newWriteIndex = writeIndex + 1;
            impl_->controlBlock->writeIndex = newWriteIndex;
            impl_->controlBlock->totalFrames++;

            // Increase the frame count if we haven't wrapped around yet
            uint64_t readIndex = impl_->controlBlock->readIndex;
            if (newWriteIndex > readIndex) {
                impl_->controlBlock->frameCount = newWriteIndex - readIndex;
            } else {
                impl_->controlBlock->frameCount = impl_->maxFrames;
            }

            return Status::OK;
        }

        SharedMemory::Status SharedMemory::readLatestFrame(std::shared_ptr<Frame> &frame) const {
            if (!isInitialized_ || !impl_->controlBlock) {
                return Status::NOT_INITIALIZED;
            }

            // Get the current write and read indices
            uint64_t writeIndex = impl_->controlBlock->writeIndex;
            uint64_t readIndex = impl_->controlBlock->readIndex;

            if (writeIndex == 0 || writeIndex <= readIndex) {
                return Status::BUFFER_EMPTY;
            }

            // Get the latest frame (the one just before the write index)
            const uint64_t latestIndex = writeIndex - 1;

            // Get the frame header
            const FrameHeader *header = impl_->getFrameHeader(latestIndex);
            if (!header) {
                return Status::INTERNAL_ERROR;
            }

            // Get the frame data
            void *dataPtr = impl_->getFrameData(latestIndex, sizeof(FrameHeader));
            if (!dataPtr) {
                return Status::INTERNAL_ERROR;
            }

            // Create a new frame that refers to the shared memory data (zero-copy)
            std::string format;
            switch (header->formatCode) {
                case 0x01: format = "YUV";
                    break;
                case 0x02: format = "BGRA";
                    break;
                case 0x03: format = "YUV10";
                    break;
                case 0x04: format = "RGB10";
                    break;
                default: format = "Unknown";
                    break;
            }

            frame = Frame::createWithExternalData(
                dataPtr, header->dataSize, header->width, header->height,
                header->bytesPerPixel, format, false);

            if (!frame) {
                return Status::INTERNAL_ERROR;
            }

            // Set the frame ID and timestamp
            frame->setFrameId(header->frameId);
            const auto timestamp = std::chrono::system_clock::time_point(
                std::chrono::nanoseconds(header->timestamp));
            frame->setTimestamp(timestamp);

            // Don't update the read index for readLatestFrame

            return Status::OK;
        }

        SharedMemory::Status SharedMemory::readNextFrame(std::shared_ptr<Frame> &frame, const unsigned int waitMilliseconds) const {
            if (!isInitialized_ || !impl_->controlBlock) {
                return Status::NOT_INITIALIZED;
            }

            // Get the current read index
            uint64_t readIndex = impl_->controlBlock->readIndex;
            uint64_t writeIndex = impl_->controlBlock->writeIndex;

            // Check if there are any frames available
            if (readIndex >= writeIndex) {
                if (waitMilliseconds == 0) {
                    return Status::BUFFER_EMPTY;
                }

                // Wait for a new frame
                unsigned int elapsed = 0;

                while (readIndex >= writeIndex && elapsed < waitMilliseconds) {
                    constexpr unsigned int sleepMs = 5;
                    usleep(sleepMs * 1000);
                    elapsed += sleepMs;

                    // Check again
                    writeIndex = impl_->controlBlock->writeIndex;
                }

                if (readIndex >= writeIndex) {
                    return Status::TIMEOUT;
                }
            }

            // Get the frame header
            const FrameHeader *header = impl_->getFrameHeader(readIndex);
            if (!header) {
                return Status::INTERNAL_ERROR;
            }

            // Get the frame data
            void *dataPtr = impl_->getFrameData(readIndex, sizeof(FrameHeader));
            if (!dataPtr) {
                return Status::INTERNAL_ERROR;
            }

            // Create a new frame that refers to the shared memory data (zero-copy)
            std::string format;
            switch (header->formatCode) {
                case 0x01: format = "YUV";
                    break;
                case 0x02: format = "BGRA";
                    break;
                case 0x03: format = "YUV10";
                    break;
                case 0x04: format = "RGB10";
                    break;
                default: format = "Unknown";
                    break;
            }

            frame = Frame::createWithExternalData(
                dataPtr, header->dataSize, header->width, header->height,
                header->bytesPerPixel, format, false);

            if (!frame) {
                return Status::INTERNAL_ERROR;
            }

            // Set the frame ID and timestamp
            frame->setFrameId(header->frameId);
            const auto timestamp = std::chrono::system_clock::time_point(
                std::chrono::nanoseconds(header->timestamp));
            frame->setTimestamp(timestamp);

            // Update the read index
            impl_->controlBlock->readIndex = readIndex + 1;

            // Update the frame count
            if (impl_->controlBlock->frameCount > 0) {
                --impl_->controlBlock->frameCount;
            }

            return Status::OK;
        }

        SharedMemory::Status SharedMemory::registerFrameCallback(std::function<void(std::shared_ptr<Frame>)> callback) {
            std::unique_lock lock(callbackMutex_);

            // Store the callback
            frameCallback_ = std::move(callback);

            // Start the notification thread if we're initialized
            if (isInitialized_ && !isServer_ && !callbackThread_.joinable()) {
                stopCallbackThread_ = false;
                callbackThread_ = std::thread(&SharedMemory::notificationThread, this);
            }

            return Status::OK;
        }

        SharedMemory::Status SharedMemory::unregisterFrameCallback() {
            std::unique_lock lock(callbackMutex_);

            // Stop the notification thread if it's running
            if (callbackThread_.joinable()) {
                stopCallbackThread_ = true;
                lock.unlock();
                callbackThread_.join();
                lock.lock();
            }

            // Clear the callback
            frameCallback_ = nullptr;

            return Status::OK;
        }

        std::string SharedMemory::getName() const {
            return name_;
        }

        size_t SharedMemory::getSize() const {
            return size_;
        }

        void SharedMemory::notificationThread() {
            // This thread runs in the client to monitor for new frames

            if (!isInitialized_ || !impl_->controlBlock) {
                return;
            }

            // Initial read index
            uint64_t lastReadIndex = impl_->controlBlock->readIndex;

            while (!stopCallbackThread_) {
                // Get the current indices
                const uint64_t writeIndex = impl_->controlBlock->writeIndex;
                uint64_t readIndex = impl_->controlBlock->readIndex;

                // Check if there are new frames
                if (writeIndex > lastReadIndex) {
                    // Process all new frames
                    for (uint64_t i = lastReadIndex; i < writeIndex; ++i) {
                        // Get the frame
                        std::shared_ptr<Frame> frame;

                        if (const Status status = readNextFrame(frame, 0); status == Status::OK && frame) {
                            // Call the callback
                            std::unique_lock lock(callbackMutex_);
                            if (frameCallback_) {
                                frameCallback_(frame);
                            }
                        }
                    }

                    // Update the last read index
                    lastReadIndex = writeIndex;
                }

                // Sleep for a short time
                usleep(5000); // 5ms
            }
        }
    } // namespace medical::imaging

