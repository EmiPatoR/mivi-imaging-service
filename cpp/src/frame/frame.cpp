#include "frame/frame.h"
#include <cstring>
#include <map>
#include <iostream>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <algorithm>

namespace medical {
    namespace imaging {
        /**
         * @brief Private implementation of Frame
         */
        struct Frame::Impl {
            void *data; // Pointer to raw frame data
            size_t dataSize; // Size of the data in bytes
            int width; // Frame width in pixels
            int height; // Frame height in pixels
            int bytesPerPixel; // Number of bytes per pixel
            std::string format; // Pixel format string
            uint64_t frameId; // Unique frame ID
            std::chrono::system_clock::time_point timestamp; // Frame timestamp
            bool ownsData; // Whether this frame owns the data buffer
            BufferType bufferType; // Type of buffer
            FrameMetadata metadata; // Enhanced metadata

            // Shared memory mapping info
            bool isMapped; // Whether this frame is mapped to shared memory
            std::string shmName; // Name of shared memory region
            size_t shmOffset; // Offset within shared memory
            int shmFd; // File descriptor for shared memory

            // GPU memory info
            void *gpuPtr; // GPU memory pointer (if using GPU memory)

            // Lock state
            bool isLocked; // Whether buffer is currently locked
            bool isLockedForWriting; // Whether buffer is locked for writing

            // Constructor
            Impl() : data(nullptr),
                     dataSize(0),
                     width(0),
                     height(0),
                     bytesPerPixel(0),
                     format(""),
                     frameId(0),
                     timestamp(std::chrono::system_clock::now()),
                     ownsData(false),
                     bufferType(BufferType::CPU_MEMORY),
                     isMapped(false),
                     shmName(""),
                     shmOffset(0),
                     shmFd(-1),
                     gpuPtr(nullptr),
                     isLocked(false),
                     isLockedForWriting(false) {
            }

            // Destructor
            ~Impl() {
                // Unlock if still locked
                if (isLocked) {
                    if (bufferType == BufferType::GPU_MEMORY) {
                        // TODO: Implement GPU unlock if needed
                    }
                }

                // Free CPU memory if owned
                if (ownsData && data && bufferType == BufferType::CPU_MEMORY) {
                    free(data);
                    data = nullptr;
                }

                // Unmap shared memory if mapped
                if (isMapped && data) {
                    munmap(data, dataSize);
                    data = nullptr;

                    if (shmFd >= 0) {
                        close(shmFd);
                        shmFd = -1;
                    }
                }

                // Free GPU memory if owned
                if (ownsData && gpuPtr && bufferType == BufferType::GPU_MEMORY) {
                    // TODO: Implement GPU memory freeing if needed
                    gpuPtr = nullptr;
                }
            }

            // Initialize CPU memory
            bool initializeCPUMemory(int w, int h, int bpp, const std::string &fmt) {
                width = w;
                height = h;
                bytesPerPixel = bpp;
                format = fmt;
                dataSize = static_cast<size_t>(width) * height * bytesPerPixel;
                bufferType = BufferType::CPU_MEMORY;

                // Allocate memory
                data = malloc(dataSize);
                if (!data) {
                    return false;
                }

                ownsData = true;
                return true;
            }

            // Initialize with external CPU memory
            bool initializeExternalCPUMemory(void *ptr, size_t size, int w, int h, int bpp,
                                             const std::string &fmt, bool takeOwnership) {
                if (!ptr || size == 0) {
                    return false;
                }

                width = w;
                height = h;
                bytesPerPixel = bpp;
                format = fmt;
                dataSize = size;
                bufferType = BufferType::CPU_MEMORY;

                data = ptr;
                ownsData = takeOwnership;

                return true;
            }

            // Initialize with shared memory mapping
            bool initializeSharedMemoryMapping(const std::string &name, size_t offset, size_t size,
                                               int w, int h, int bpp, const std::string &fmt) {
                width = w;
                height = h;
                bytesPerPixel = bpp;
                format = fmt;
                dataSize = size;
                bufferType = BufferType::CPU_MEMORY; // Still uses CPU memory, just mapped

                // Store shared memory info
                shmName = name;
                shmOffset = offset;
                isMapped = true;

                // Open the shared memory
                shmFd = shm_open(name.c_str(), O_RDWR, 0666);
                if (shmFd < 0) {
                    std::cerr << "Failed to open shared memory: " << strerror(errno) << std::endl;
                    return false;
                }

                // Map the region
                data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, shmFd, offset);
                if (data == MAP_FAILED) {
                    std::cerr << "Failed to map shared memory: " << strerror(errno) << std::endl;
                    close(shmFd);
                    shmFd = -1;
                    data = nullptr;
                    return false;
                }

                ownsData = false; // Data is owned by the shared memory

                return true;
            }

            // Initialize with GPU memory
            bool initializeGPUMemory(void *gpuMemory, size_t size, int w, int h, int bpp,
                                     const std::string &fmt, bool takeOwnership) {
                width = w;
                height = h;
                bytesPerPixel = bpp;
                format = fmt;
                dataSize = size;
                bufferType = BufferType::GPU_MEMORY;

                // Store GPU pointer
                gpuPtr = gpuMemory;

                // For now, just point data to nullptr
                data = nullptr;

                ownsData = takeOwnership;

                return true;
            }

            // Lock buffer for CPU access
            bool lockForCPU(bool forWriting) {
                if (isLocked) {
                    // Already locked - check if the lock type is compatible
                    if (isLockedForWriting || !forWriting) {
                        return true; // Compatible lock
                    }
                    return false; // Incompatible lock
                }

                switch (bufferType) {
                    case BufferType::CPU_MEMORY:
                        // Already in CPU memory - just mark as locked
                        isLocked = true;
                        isLockedForWriting = forWriting;
                        return true;

                    case BufferType::GPU_MEMORY:
                        // TODO: Implement GPU to CPU copy if needed
                        return false;

                    case BufferType::DMA_BUFFER:
                        // TODO: Implement DMA buffer locking if needed
                        return false;

                    case BufferType::EXTERNAL_MEMORY:
                        // External memory handling depends on the specific memory type
                        return false;
                }

                return false;
            }

            // Unlock buffer
            void unlock() {
                if (!isLocked) {
                    return;
                }

                switch (bufferType) {
                    case BufferType::CPU_MEMORY:
                        // Just mark as unlocked
                        isLocked = false;
                        isLockedForWriting = false;
                        break;

                    case BufferType::GPU_MEMORY:
                        // TODO: Implement GPU unlock if needed
                        isLocked = false;
                        isLockedForWriting = false;
                        break;

                    case BufferType::DMA_BUFFER:
                        // TODO: Implement DMA buffer unlocking if needed
                        isLocked = false;
                        isLockedForWriting = false;
                        break;

                    case BufferType::EXTERNAL_MEMORY:
                        // External memory handling depends on the specific memory type
                        isLocked = false;
                        isLockedForWriting = false;
                        break;
                }
            }
        };

        Frame::Frame() : impl_(std::make_unique<Impl>()) {
        }

        Frame::~Frame() {
            // Call the destroy callback if set
            if (onDestroyCallback_) {
                onDestroyCallback_();
            }

            // impl_ destructor will clean up resources
        }

        std::shared_ptr<Frame> Frame::create(
            int width, int height, int bytesPerPixel, const std::string &format,
            BufferType bufferType) {
            // Create new frame
            std::shared_ptr<Frame> frame = std::shared_ptr<Frame>(new Frame());

            // Initialize based on buffer type
            switch (bufferType) {
                case BufferType::CPU_MEMORY:
                    // Standard CPU memory allocation
                    if (!frame->impl_->initializeCPUMemory(width, height, bytesPerPixel, format)) {
                        return nullptr;
                    }
                    break;

                case BufferType::GPU_MEMORY:
                    // TODO: Implement GPU memory allocation if needed
                    return nullptr;

                case BufferType::DMA_BUFFER:
                    // TODO: Implement DMA buffer allocation if needed
                    return nullptr;

                case BufferType::EXTERNAL_MEMORY:
                    // Can't create with external memory without providing it
                    return nullptr;
            }

            // Set initial timestamp
            frame->impl_->timestamp = std::chrono::system_clock::now();

            // Set frame ID based on timestamp
            auto now = std::chrono::high_resolution_clock::now();
            auto duration = now.time_since_epoch();
            auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
            frame->impl_->frameId = static_cast<uint64_t>(nanos);

            return frame;
        }

        std::shared_ptr<Frame> Frame::createWithExternalData(
            void *data, size_t size, int width, int height, int bytesPerPixel,
            const std::string &format, bool ownsData, BufferType bufferType) {
            if (!data || size == 0) {
                return nullptr;
            }

            // Create new frame
            std::shared_ptr<Frame> frame = std::shared_ptr<Frame>(new Frame());

            // Initialize based on buffer type
            switch (bufferType) {
                case BufferType::CPU_MEMORY:
                    // Standard CPU memory
                    if (!frame->impl_->initializeExternalCPUMemory(
                        data, size, width, height, bytesPerPixel, format, ownsData)) {
                        return nullptr;
                    }
                    break;

                case BufferType::GPU_MEMORY:
                    // GPU memory
                    if (!frame->impl_->initializeGPUMemory(
                        data, size, width, height, bytesPerPixel, format, ownsData)) {
                        return nullptr;
                    }
                    break;

                case BufferType::DMA_BUFFER:
                    // TODO: Implement DMA buffer handling if needed
                    return nullptr;

                case BufferType::EXTERNAL_MEMORY:
                    // Generic external memory - just store the pointer
                    if (!frame->impl_->initializeExternalCPUMemory(
                        data, size, width, height, bytesPerPixel, format, ownsData)) {
                        return nullptr;
                    }
                    frame->impl_->bufferType = BufferType::EXTERNAL_MEMORY;
                    break;
            }

            // Set initial timestamp
            frame->impl_->timestamp = std::chrono::system_clock::now();

            // Set frame ID based on timestamp
            auto now = std::chrono::high_resolution_clock::now();
            auto duration = now.time_since_epoch();
            auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
            frame->impl_->frameId = static_cast<uint64_t>(nanos);

            return frame;
        }

        std::shared_ptr<Frame> Frame::createMapped(
            const std::string &shmName, size_t offset, size_t size,
            int width, int height, int bytesPerPixel, const std::string &format) {
            // Create new frame
            std::shared_ptr<Frame> frame = std::shared_ptr<Frame>(new Frame());

            // Initialize with shared memory mapping
            if (!frame->impl_->initializeSharedMemoryMapping(
                shmName, offset, size, width, height, bytesPerPixel, format)) {
                return nullptr;
            }

            // Set initial timestamp
            frame->impl_->timestamp = std::chrono::system_clock::now();

            // Set frame ID based on timestamp
            auto now = std::chrono::high_resolution_clock::now();
            auto duration = now.time_since_epoch();
            auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
            frame->impl_->frameId = static_cast<uint64_t>(nanos);

            return frame;
        }

        void *Frame::getData() const {
            return impl_->data;
        }

        size_t Frame::getDataSize() const {
            return impl_->dataSize;
        }

        int Frame::getWidth() const {
            return impl_->width;
        }

        int Frame::getHeight() const {
            return impl_->height;
        }

        int Frame::getBytesPerPixel() const {
            return impl_->bytesPerPixel;
        }

        std::string Frame::getFormat() const {
            return impl_->format;
        }

        std::chrono::system_clock::time_point Frame::getTimestamp() const {
            return impl_->timestamp;
        }

        void Frame::setTimestamp(std::chrono::system_clock::time_point timestamp) {
            impl_->timestamp = timestamp;
        }

        uint64_t Frame::getFrameId() const {
            return impl_->frameId;
        }

        void Frame::setFrameId(uint64_t id) {
            impl_->frameId = id;
        }

        BufferType Frame::getBufferType() const {
            return impl_->bufferType;
        }

        const FrameMetadata &Frame::getMetadata() const {
            return impl_->metadata;
        }

        FrameMetadata &Frame::getMetadataMutable() {
            return impl_->metadata;
        }

        bool Frame::isGpuMemory() const {
            return impl_->bufferType == BufferType::GPU_MEMORY;
        }

        bool Frame::isDmaBuffer() const {
            return impl_->bufferType == BufferType::DMA_BUFFER;
        }

        bool Frame::isMappedToSharedMemory() const {
            return impl_->isMapped;
        }

        std::shared_ptr<Frame> Frame::clone(BufferType targetBufferType) const {
            // Create a new frame with the specified buffer type
            auto newFrame = Frame::create(
                impl_->width,
                impl_->height,
                impl_->bytesPerPixel,
                impl_->format,
                targetBufferType);

            if (!newFrame) {
                return nullptr;
            }

            // Lock the source and destination for access
            bool srcLocked = const_cast<Frame *>(this)->lock(true);
            bool dstLocked = newFrame->lock(false);

            if (!srcLocked || !dstLocked) {
                if (srcLocked) const_cast<Frame *>(this)->unlock();
                if (dstLocked) newFrame->unlock();
                return nullptr;
            }

            // Copy data
            std::memcpy(newFrame->impl_->data, impl_->data, impl_->dataSize);

            // Unlock
            const_cast<Frame *>(this)->unlock();
            newFrame->unlock();

            // Copy metadata
            newFrame->impl_->frameId = impl_->frameId;
            newFrame->impl_->timestamp = impl_->timestamp;
            newFrame->impl_->metadata = impl_->metadata;

            return newFrame;
        }

        void Frame::setMetadata(const std::string &key, const std::string &value) {
            impl_->metadata.attributes[key] = value;
        }

        std::string Frame::getMetadata(const std::string &key) const {
            auto it = impl_->metadata.attributes.find(key);
            if (it != impl_->metadata.attributes.end()) {
                return it->second;
            }

            return "";
        }

        bool Frame::lock(bool readOnly) {
            return impl_->lockForCPU(!readOnly);
        }

        void Frame::unlock() {
            impl_->unlock();
        }

        bool Frame::exportToSharedMemory(const std::string &shmName, size_t offset) {
            // TODO: Implement export to shared memory if needed
            return false;
        }
    } // namespace imaging
} // namespace medical
