#include "communication/shared_memory.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <thread>
#include <chrono>
#include <utility>
#include <pthread.h>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>  // For JSON metadata

using json = nlohmann::json;

namespace medical::imaging {
    // Platform-specific implementation for shared memory
    struct SharedMemory::Impl {
        // Common members
        SharedMemoryType type;
        std::string name;
        size_t size;
        bool isServer;
        void *mapping;

        // POSIX shared memory specific
        int fd;

        // System V shared memory specific
        int shmid;

        // Memory-mapped file specific
        std::string filePath;

        // Control block structure - stored at the beginning of shared memory
        struct ControlBlock {
            std::atomic<uint64_t> writeIndex; // Current write position
            std::atomic<uint64_t> readIndex; // Current read position
            std::atomic<uint64_t> frameCount; // Number of frames in the buffer
            std::atomic<uint64_t> totalFramesWritten; // Total number of frames written
            std::atomic<uint64_t> totalFramesRead; // Total number of frames read
            std::atomic<uint64_t> droppedFrames; // Frames dropped due to buffer full
            std::atomic<bool> active; // Whether the shared memory is active
            std::atomic<uint64_t> lastWriteTime; // Timestamp of last write (ns since epoch)
            std::atomic<uint64_t> lastReadTime; // Timestamp of last read (ns since epoch)
            std::atomic<uint64_t> maxFrameSize; // Maximum frame size seen
            uint32_t metadataOffset; // Offset to JSON metadata
            uint32_t metadataSize; // Size of metadata area
            std::atomic<uint32_t> flags; // Additional flags
            uint8_t padding[200]; // Padding to ensure proper alignment
        };

        ControlBlock *controlBlock; // Pointer to control block in shared memory
        size_t controlBlockSize; // Size of the control block
        size_t dataOffset; // Offset to the start of the data region
        size_t maxFrames; // Maximum number of frames in the ring buffer

        // Constructor
        Impl() : type(SharedMemoryType::POSIX_SHM), fd(-1), shmid(-1), mapping(nullptr),
                 controlBlock(nullptr), controlBlockSize(0), dataOffset(0), maxFrames(0) {
        }

        // Destructor
        ~Impl() {
            cleanup();
        }

        // Cleanup resources
        void cleanup() {
            // Unmap memory if mapped
            if (mapping) {
                munmap(mapping, size);
                mapping = nullptr;
            }

            // Close file descriptor if open (POSIX)
            if (fd >= 0) {
                close(fd);
                fd = -1;
            }

            // Detach from System V if attached
            if (shmid >= 0) {
                shmdt(mapping); // Already unmapped, but for clarity
                shmid = -1;
            }

            // Unlink if server
            if (isServer) {
                switch (type) {
                    case SharedMemoryType::POSIX_SHM:
                        shm_unlink(name.c_str());
                        break;
                    case SharedMemoryType::SYSV_SHM:
                        if (shmid >= 0) {
                            shmctl(shmid, IPC_RMID, nullptr);
                        }
                        break;
                    case SharedMemoryType::MEMORY_MAPPED_FILE:
                        // File remains on disk - don't unlink by default
                        break;
                    case SharedMemoryType::HUGE_PAGES:
                        shm_unlink(name.c_str());
                        break;
                }
            }
        }

        // Initialize POSIX shared memory
        SharedMemory::Status initializePosixShm(const std::string &shmName, size_t shmSize, bool create) {
            name = "/" + shmName; // POSIX shared memory names must start with '/'
            size = shmSize;
            isServer = create;
            type = SharedMemoryType::POSIX_SHM;

            // Calculate control block size and data offset
            controlBlockSize = sizeof(ControlBlock);
            dataOffset = controlBlockSize;

            // Size must be large enough for the control block and at least one frame
            if (size <= dataOffset + sizeof(FrameHeader)) {
                return SharedMemory::Status::INVALID_SIZE;
            }

            if (create) {
                // Create the shared memory as the server
                fd = shm_open(name.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
                if (fd < 0) {
                    std::cerr << "Failed to create shared memory: " << strerror(errno) << std::endl;
                    return SharedMemory::Status::CREATION_FAILED;
                }

                // Set the size
                if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
                    std::cerr << "Failed to set shared memory size: " << strerror(errno) << std::endl;
                    close(fd);
                    fd = -1;
                    shm_unlink(name.c_str());
                    return SharedMemory::Status::CREATION_FAILED;
                }
            } else {
                // Open existing shared memory as a client
                fd = shm_open(name.c_str(), O_RDWR, S_IRUSR | S_IWUSR);
                if (fd < 0) {
                    std::cerr << "Failed to open shared memory: " << strerror(errno) << std::endl;
                    return SharedMemory::Status::CREATION_FAILED;
                }

                // Get the actual size
                struct stat sb{};
                if (fstat(fd, &sb) < 0) {
                    std::cerr << "Failed to stat shared memory: " << strerror(errno) << std::endl;
                    close(fd);
                    fd = -1;
                    return SharedMemory::Status::CREATION_FAILED;
                }

                size = static_cast<size_t>(sb.st_size);
            }

            // Map the shared memory into our address space
            mapping = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (mapping == MAP_FAILED) {
                std::cerr << "Failed to map shared memory: " << strerror(errno) << std::endl;
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
                controlBlock->totalFramesWritten = 0;
                controlBlock->totalFramesRead = 0;
                controlBlock->droppedFrames = 0;
                controlBlock->active = true;
                controlBlock->lastWriteTime = 0;
                controlBlock->lastReadTime = 0;
                controlBlock->maxFrameSize = 0;
                controlBlock->metadataOffset = static_cast<uint32_t>(dataOffset);
                controlBlock->metadataSize = 4096; // 4KB for metadata area
                controlBlock->flags = 0;

                // Update data offset to account for metadata area
                dataOffset += controlBlock->metadataSize;

                // Calculate the maximum number of frames that can fit
                // Assuming average frame size of 1080p YUV (1920x1080x2 bytes + header)
                size_t avgFrameSize = 1920 * 1080 * 2 + sizeof(FrameHeader);
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
                    std::cerr << "Timeout waiting for shared memory to be initialized" << std::endl;
                    munmap(mapping, size);
                    mapping = nullptr;
                    close(fd);
                    fd = -1;
                    return SharedMemory::Status::INTERNAL_ERROR;
                }

                // Get the data offset including metadata
                dataOffset = controlBlockSize + controlBlock->metadataSize;

                // Get the maximum number of frames
                size_t avgFrameSize = 1920 * 1080 * 2 + sizeof(FrameHeader);
                maxFrames = (size - dataOffset) / avgFrameSize;
            }

            return SharedMemory::Status::OK;
        }

        // Initialize System V shared memory
        SharedMemory::Status initializeSysVShm(const std::string &shmName, size_t shmSize, bool create) {
            name = shmName;
            size = shmSize;
            isServer = create;
            type = SharedMemoryType::SYSV_SHM;

            // Calculate control block size and data offset
            controlBlockSize = sizeof(ControlBlock);
            dataOffset = controlBlockSize;

            // Size must be large enough for the control block and at least one frame
            if (size <= dataOffset + sizeof(FrameHeader)) {
                return SharedMemory::Status::INVALID_SIZE;
            }

            // Generate a key from the name using ftok
            key_t key = ftok(name.c_str(), 1);
            if (key == -1) {
                // If the file doesn't exist, create a temporary one
                if (errno == ENOENT && create) {
                    std::ofstream tempFile(name);
                    tempFile.close();
                    key = ftok(name.c_str(), 1);
                    if (key == -1) {
                        std::cerr << "Failed to create key for SysV shared memory: " << strerror(errno) << std::endl;
                        return SharedMemory::Status::CREATION_FAILED;
                    }
                } else {
                    std::cerr << "Failed to create key for SysV shared memory: " << strerror(errno) << std::endl;
                    return SharedMemory::Status::CREATION_FAILED;
                }
            }

            if (create) {
                // Create the shared memory segment
                shmid = shmget(key, size, IPC_CREAT | IPC_EXCL | 0666);
                if (shmid == -1) {
                    // If already exists, try to get it
                    if (errno == EEXIST) {
                        shmid = shmget(key, size, 0666);
                    }

                    if (shmid == -1) {
                        std::cerr << "Failed to create SysV shared memory: " << strerror(errno) << std::endl;
                        return SharedMemory::Status::CREATION_FAILED;
                    }
                }
            } else {
                // Open existing shared memory segment
                shmid = shmget(key, 0, 0666);
                if (shmid == -1) {
                    std::cerr << "Failed to open SysV shared memory: " << strerror(errno) << std::endl;
                    return SharedMemory::Status::CREATION_FAILED;
                }

                // Get the actual size
                struct shmid_ds shmInfo;
                if (shmctl(shmid, IPC_STAT, &shmInfo) == -1) {
                    std::cerr << "Failed to get SysV shared memory info: " << strerror(errno) << std::endl;
                    return SharedMemory::Status::INTERNAL_ERROR;
                }

                size = shmInfo.shm_segsz;
            }

            // Attach to the shared memory segment
            mapping = shmat(shmid, nullptr, 0);
            if (mapping == reinterpret_cast<void *>(-1)) {
                std::cerr << "Failed to attach to SysV shared memory: " << strerror(errno) << std::endl;
                mapping = nullptr;
                if (create) {
                    shmctl(shmid, IPC_RMID, nullptr);
                }
                shmid = -1;
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
                controlBlock->totalFramesWritten = 0;
                controlBlock->totalFramesRead = 0;
                controlBlock->droppedFrames = 0;
                controlBlock->active = true;
                controlBlock->lastWriteTime = 0;
                controlBlock->lastReadTime = 0;
                controlBlock->maxFrameSize = 0;
                controlBlock->metadataOffset = static_cast<uint32_t>(dataOffset);
                controlBlock->metadataSize = 4096; // 4KB for metadata area
                controlBlock->flags = 0;

                // Update data offset to account for metadata area
                dataOffset += controlBlock->metadataSize;

                // Calculate the maximum number of frames that can fit
                size_t avgFrameSize = 1920 * 1080 * 2 + sizeof(FrameHeader);
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
                    std::cerr << "Timeout waiting for SysV shared memory to be initialized" << std::endl;
                    shmdt(mapping);
                    mapping = nullptr;
                    shmid = -1;
                    return SharedMemory::Status::INTERNAL_ERROR;
                }

                // Get the data offset including metadata
                dataOffset = controlBlockSize + controlBlock->metadataSize;

                // Get the maximum number of frames
                size_t avgFrameSize = 1920 * 1080 * 2 + sizeof(FrameHeader);
                maxFrames = (size - dataOffset) / avgFrameSize;
            }

            return SharedMemory::Status::OK;
        }

        // Initialize memory-mapped file
        SharedMemory::Status initializeMemoryMappedFile(const std::string &filePath, size_t shmSize, bool create) {
            this->filePath = filePath;
            name = filePath; // Use file path as name
            size = shmSize;
            isServer = create;
            type = SharedMemoryType::MEMORY_MAPPED_FILE;

            // Calculate control block size and data offset
            controlBlockSize = sizeof(ControlBlock);
            dataOffset = controlBlockSize;

            // Size must be large enough for the control block and at least one frame
            if (size <= dataOffset + sizeof(FrameHeader)) {
                return SharedMemory::Status::INVALID_SIZE;
            }

            // Open or create the file
            int flags = O_RDWR;
            if (create) {
                flags |= O_CREAT;
            }

            fd = open(filePath.c_str(), flags, S_IRUSR | S_IWUSR);
            if (fd < 0) {
                std::cerr << "Failed to open memory-mapped file: " << strerror(errno) << std::endl;
                return SharedMemory::Status::CREATION_FAILED;
            }

            if (create) {
                // Set the file size
                if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
                    std::cerr << "Failed to set memory-mapped file size: " << strerror(errno) << std::endl;
                    close(fd);
                    fd = -1;
                    return SharedMemory::Status::CREATION_FAILED;
                }
            } else {
                // Get the actual file size
                struct stat sb{};
                if (fstat(fd, &sb) < 0) {
                    std::cerr << "Failed to stat memory-mapped file: " << strerror(errno) << std::endl;
                    close(fd);
                    fd = -1;
                    return SharedMemory::Status::CREATION_FAILED;
                }

                size = static_cast<size_t>(sb.st_size);
            }

            // Map the file into memory
            mapping = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (mapping == MAP_FAILED) {
                std::cerr << "Failed to map file: " << strerror(errno) << std::endl;
                mapping = nullptr;
                close(fd);
                fd = -1;
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
                controlBlock->totalFramesWritten = 0;
                controlBlock->totalFramesRead = 0;
                controlBlock->droppedFrames = 0;
                controlBlock->active = true;
                controlBlock->lastWriteTime = 0;
                controlBlock->lastReadTime = 0;
                controlBlock->maxFrameSize = 0;
                controlBlock->metadataOffset = static_cast<uint32_t>(dataOffset);
                controlBlock->metadataSize = 4096; // 4KB for metadata area
                controlBlock->flags = 0;

                // Update data offset to account for metadata area
                dataOffset += controlBlock->metadataSize;

                // Calculate the maximum number of frames that can fit
                size_t avgFrameSize = 1920 * 1080 * 2 + sizeof(FrameHeader);
                maxFrames = (size - dataOffset) / avgFrameSize;
                if (maxFrames < 1) {
                    maxFrames = 1;
                }

                // Initialize metadata area with empty JSON
                json metadata = {
                    {"format_version", "1.0"},
                    {"created_at", std::chrono::system_clock::now().time_since_epoch().count()},
                    {"type", "medical_imaging_frames"},
                    {"frame_format", ""},
                    {"max_frames", maxFrames},
                    {"buffer_size", size},
                    {"data_offset", dataOffset}
                };

                // Write JSON to metadata area
                std::string metadataStr = metadata.dump();
                char *metadataArea = static_cast<char *>(mapping) + controlBlock->metadataOffset;
                std::strncpy(metadataArea, metadataStr.c_str(), controlBlock->metadataSize - 1);
                metadataArea[controlBlock->metadataSize - 1] = '\0';
            } else {
                // Client just uses the existing control block
                // Wait for it to be initialized
                int attempts = 0;
                while (!controlBlock->active && attempts < 100) {
                    usleep(10000); // 10ms
                    attempts++;
                }

                if (!controlBlock->active) {
                    std::cerr << "Timeout waiting for memory-mapped file to be initialized" << std::endl;
                    munmap(mapping, size);
                    mapping = nullptr;
                    close(fd);
                    fd = -1;
                    return SharedMemory::Status::INTERNAL_ERROR;
                }

                // Get the data offset including metadata
                dataOffset = controlBlockSize + controlBlock->metadataSize;

                // Get the maximum number of frames
                size_t avgFrameSize = 1920 * 1080 * 2 + sizeof(FrameHeader);
                maxFrames = (size - dataOffset) / avgFrameSize;
            }

            return SharedMemory::Status::OK;
        }

        // Initialize huge pages shared memory
        SharedMemory::Status initializeHugePages(const std::string &shmName, size_t shmSize, bool create) {
            // Check if huge pages are available on the system
            std::ifstream hugePageInfo("/proc/meminfo");
            bool hugePageAvailable = false;
            std::string line;
            size_t hugePageSize = 0;

            while (std::getline(hugePageInfo, line)) {
                if (line.find("Hugepagesize:") != std::string::npos) {
                    // Parse the huge page size
                    std::istringstream iss(line);
                    std::string dummy;
                    iss >> dummy >> hugePageSize;
                    hugePageAvailable = (hugePageSize > 0);
                    hugePageSize *= 1024; // Convert KB to bytes
                    break;
                }
            }

            if (!hugePageAvailable) {
                std::cerr << "Huge pages not available on this system" << std::endl;
                return SharedMemory::Status::NOT_SUPPORTED;
            }

            // Round up size to a multiple of huge page size
            size_t roundedSize = ((shmSize + hugePageSize - 1) / hugePageSize) * hugePageSize;
            if (roundedSize != shmSize) {
                std::cout << "Rounding shared memory size to " << roundedSize
                        << " bytes (multiple of huge page size " << hugePageSize << ")" << std::endl;
                shmSize = roundedSize;
            }

            // Now use POSIX shared memory with MAP_HUGETLB flag
            name = "/" + shmName; // POSIX shared memory names must start with '/'
            size = shmSize;
            isServer = create;
            type = SharedMemoryType::HUGE_PAGES;

            // Calculate control block size and data offset
            controlBlockSize = sizeof(ControlBlock);
            dataOffset = controlBlockSize;

            // Size must be large enough for the control block and at least one frame
            if (size <= dataOffset + sizeof(FrameHeader)) {
                return SharedMemory::Status::INVALID_SIZE;
            }

            if (create) {
                // Create the shared memory as the server
                fd = shm_open(name.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
                if (fd < 0) {
                    std::cerr << "Failed to create shared memory: " << strerror(errno) << std::endl;
                    return SharedMemory::Status::CREATION_FAILED;
                }

                // Set the size
                if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
                    std::cerr << "Failed to set shared memory size: " << strerror(errno) << std::endl;
                    close(fd);
                    fd = -1;
                    shm_unlink(name.c_str());
                    return SharedMemory::Status::CREATION_FAILED;
                }
            } else {
                // Open existing shared memory as a client
                fd = shm_open(name.c_str(), O_RDWR, S_IRUSR | S_IWUSR);
                if (fd < 0) {
                    std::cerr << "Failed to open shared memory: " << strerror(errno) << std::endl;
                    return SharedMemory::Status::CREATION_FAILED;
                }

                // Get the actual size
                struct stat sb{};
                if (fstat(fd, &sb) < 0) {
                    std::cerr << "Failed to stat shared memory: " << strerror(errno) << std::endl;
                    close(fd);
                    fd = -1;
                    return SharedMemory::Status::CREATION_FAILED;
                }

                size = static_cast<size_t>(sb.st_size);
            }

            // Map the shared memory into our address space with huge pages
            mapping = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_HUGETLB, fd, 0);
            if (mapping == MAP_FAILED) {
                std::cerr << "Failed to map shared memory with huge pages: " << strerror(errno) << std::endl;

                // Fall back to regular pages
                std::cerr << "Falling back to regular pages" << std::endl;
                mapping = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                if (mapping == MAP_FAILED) {
                    std::cerr << "Failed to map shared memory: " << strerror(errno) << std::endl;
                    mapping = nullptr;
                    close(fd);
                    fd = -1;
                    if (create) {
                        shm_unlink(name.c_str());
                    }
                    return SharedMemory::Status::NOT_INITIALIZED;
                }
            }

            // Set up the control block
            controlBlock = static_cast<ControlBlock *>(mapping);

            if (create) {
                // Initialize the control block as server
                new(controlBlock) ControlBlock();
                controlBlock->writeIndex = 0;
                controlBlock->readIndex = 0;
                controlBlock->frameCount = 0;
                controlBlock->totalFramesWritten = 0;
                controlBlock->totalFramesRead = 0;
                controlBlock->droppedFrames = 0;
                controlBlock->active = true;
                controlBlock->lastWriteTime = 0;
                controlBlock->lastReadTime = 0;
                controlBlock->maxFrameSize = 0;
                controlBlock->metadataOffset = static_cast<uint32_t>(dataOffset);
                controlBlock->metadataSize = 4096; // 4KB for metadata area
                controlBlock->flags = 0;

                // Update data offset to account for metadata area
                dataOffset += controlBlock->metadataSize;

                // Calculate the maximum number of frames that can fit
                size_t avgFrameSize = 1920 * 1080 * 2 + sizeof(FrameHeader);
                maxFrames = (size - dataOffset) / avgFrameSize;
                if (maxFrames < 1) {
                    maxFrames = 1;
                }

                // Initialize metadata area with empty JSON
                json metadata = {
                    {"format_version", "1.0"},
                    {"created_at", std::chrono::system_clock::now().time_since_epoch().count()},
                    {"type", "medical_imaging_frames"},
                    {"frame_format", ""},
                    {"max_frames", maxFrames},
                    {"buffer_size", size},
                    {"data_offset", dataOffset},
                    {"using_huge_pages", true},
                    {"huge_page_size", hugePageSize}
                };

                // Write JSON to metadata area
                std::string metadataStr = metadata.dump();
                char *metadataArea = static_cast<char *>(mapping) + controlBlock->metadataOffset;
                std::strncpy(metadataArea, metadataStr.c_str(), controlBlock->metadataSize - 1);
                metadataArea[controlBlock->metadataSize - 1] = '\0';
            } else {
                // Client just uses the existing control block
                // Wait for it to be initialized
                int attempts = 0;
                while (!controlBlock->active && attempts < 100) {
                    usleep(10000); // 10ms
                    attempts++;
                }

                if (!controlBlock->active) {
                    std::cerr << "Timeout waiting for shared memory to be initialized" << std::endl;
                    munmap(mapping, size);
                    mapping = nullptr;
                    close(fd);
                    fd = -1;
                    return SharedMemory::Status::INTERNAL_ERROR;
                }

                // Get the data offset including metadata
                dataOffset = controlBlockSize + controlBlock->metadataSize;

                // Get the maximum number of frames
                size_t avgFrameSize = 1920 * 1080 * 2 + sizeof(FrameHeader);
                maxFrames = (size - dataOffset) / avgFrameSize;
            }

            return SharedMemory::Status::OK;
        }

        // Get the offset of a frame in the ring buffer
        size_t getFrameOffset(uint64_t index) const {
            return dataOffset + (index % maxFrames) * sizeof(FrameHeader);
        }

        // Get a pointer to a frame header
        FrameHeader *getFrameHeader(uint64_t index) const {
            size_t offset = getFrameOffset(index);
            if (offset + sizeof(FrameHeader) > size) {
                return nullptr;
            }

            return reinterpret_cast<FrameHeader *>(static_cast<uint8_t *>(mapping) + offset);
        }

        // Get a pointer to frame data
        void *getFrameData(uint64_t index, size_t headerSize) const {
            size_t offset = getFrameOffset(index) + headerSize;
            if (offset >= size) {
                return nullptr;
            }

            return static_cast<uint8_t *>(mapping) + offset;
        }

        // Update metadata in the shared memory
        bool updateMetadata(const json &metadata) {
            if (!controlBlock || controlBlock->metadataOffset == 0 || controlBlock->metadataSize == 0) {
                return false;
            }

            // Get the metadata area
            char *metadataArea = static_cast<char *>(mapping) + controlBlock->metadataOffset;

            // Convert JSON to string
            std::string metadataStr = metadata.dump();

            // Check if it fits
            if (metadataStr.size() >= controlBlock->metadataSize) {
                std::cerr << "Metadata too large for shared memory area" << std::endl;
                return false;
            }

            // Write it
            std::strncpy(metadataArea, metadataStr.c_str(), controlBlock->metadataSize - 1);
            metadataArea[controlBlock->metadataSize - 1] = '\0';

            return true;
        }

        // Read metadata from the shared memory
        json readMetadata() const {
            if (!controlBlock || controlBlock->metadataOffset == 0 || controlBlock->metadataSize == 0) {
                return json{};
            }

            // Get the metadata area
            char *metadataArea = static_cast<char *>(mapping) + controlBlock->metadataOffset;

            // Parse JSON
            try {
                return json::parse(metadataArea);
            } catch (const std::exception &e) {
                std::cerr << "Failed to parse metadata JSON: " << e.what() << std::endl;
                return json{};
            }
        }
    };

    // SharedMemory implementation
    SharedMemory::SharedMemory(const Config &config)
        : impl_(std::make_unique<Impl>()),
          config_(config),
          isInitialized_(false),
          stopCallbackThread_(false),
          threadAffinity_(-1),
          threadPriority_(0) {
        // Initialize statistics
        stats_ = {};
    }

    SharedMemory::~SharedMemory() {
        // Stop the notification thread if it's running
        if (callbackThread_.joinable()) {
            stopCallbackThread_ = true;
            callbackThread_.join();
        }

        // impl_ will clean up shared memory resources
    }

    SharedMemory::Status SharedMemory::initialize() {
        if (isInitialized_) {
            return Status::ALREADY_EXISTS;
        }

        Status status;

        // Initialize the appropriate type of shared memory
        switch (config_.type) {
            case SharedMemoryType::POSIX_SHM:
                status = impl_->initializePosixShm(config_.name, config_.size, config_.create);
                break;
            case SharedMemoryType::SYSV_SHM:
                status = impl_->initializeSysVShm(config_.name, config_.size, config_.create);
                break;
            case SharedMemoryType::MEMORY_MAPPED_FILE:
                status = impl_->initializeMemoryMappedFile(
                    config_.filePath.empty() ? "/dev/shm/" + config_.name : config_.filePath, config_.size,
                    config_.create);
                break;
            case SharedMemoryType::HUGE_PAGES:
                status = impl_->initializeHugePages(config_.name, config_.size, config_.create);
                break;
            default:
                return Status::NOT_SUPPORTED;
        }

        if (status != Status::OK) {
            return status;
        }

        isInitialized_ = true;

        // Start the notification thread if we're a client and have a callback
        if (!config_.create && frameCallback_) {
            stopCallbackThread_ = false;
            callbackThread_ = std::thread(&SharedMemory::notificationThread, this);

            // Set thread priority and affinity if configured
            if (threadPriority_ != 0) {
                setThreadPriority(threadPriority_);
            }

            if (threadAffinity_ >= 0) {
                setThreadAffinity(threadAffinity_);
            }
        }

        // Reset statistics
        resetStatistics();

        return Status::OK;
    }

    bool SharedMemory::isInitialized() const {
        return isInitialized_;
    }

    SharedMemory::Status SharedMemory::writeFrame(const std::shared_ptr<Frame> &frame) {
        return writeFrameTimeout(frame, 0); // Non-blocking write
    }

    SharedMemory::Status SharedMemory::writeFrameTimeout(const std::shared_ptr<Frame> &frame,
                                                         unsigned int timeoutMs) {
        if (!isInitialized_ || !impl_->controlBlock) {
            return Status::NOT_INITIALIZED;
        }

        if (!frame || !frame->getData() || frame->getDataSize() == 0) {
            return Status::INVALID_SIZE;
        }

        // Start time for performance tracking
        auto startTime = std::chrono::high_resolution_clock::now();

        // Check if the buffer is full and we need to wait
        if (impl_->controlBlock->frameCount >= impl_->maxFrames && timeoutMs > 0) {
            // Calculate end time
            auto endTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

            // Wait for space to become available
            while (impl_->controlBlock->frameCount >= impl_->maxFrames) {
                // Sleep for a short time
                std::this_thread::sleep_for(std::chrono::milliseconds(1));

                // Check if timeout has expired
                if (std::chrono::steady_clock::now() >= endTime) {
                    // Buffer still full after timeout
                    std::lock_guard<std::mutex> lock(statsMutex_);
                    stats_.bufferFullCount++;
                    stats_.droppedFrames++;
                    return Status::BUFFER_FULL;
                }
            }
        } else if (impl_->controlBlock->frameCount >= impl_->maxFrames) {
            // Buffer is full and we're not waiting
            std::lock_guard<std::mutex> lock(statsMutex_);
            stats_.bufferFullCount++;
            stats_.droppedFrames++;
            impl_->controlBlock->droppedFrames++;
            return Status::BUFFER_FULL;
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
        size_t frameOffset = impl_->getFrameOffset(writeIndex);
        if (frameOffset + requiredSize > impl_->size) {
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

        // If this frame came from shared memory originally, try to zero-copy
        if (frame->isMappedToSharedMemory()) {
            // This is already in shared memory, just update metadata
            // and avoid the copy
            header->flags |= 0x01; // Flag indicating zero-copy
        } else {
            // Copy the frame data
            void *dataPtr = impl_->getFrameData(writeIndex, sizeof(FrameHeader));
            if (!dataPtr) {
                return Status::INTERNAL_ERROR;
            }

            // Copy the frame data
            std::memcpy(dataPtr, frame->getData(), frame->getDataSize());
        }

        // Update metadata if enabled
        if (config_.enableMetadata) {
            // Read existing metadata
            auto metadata = impl_->readMetadata();

            // Update frame-specific metadata
            metadata["last_frame"]["width"] = frame->getWidth();
            metadata["last_frame"]["height"] = frame->getHeight();
            metadata["last_frame"]["format"] = frame->getFormat();
            metadata["last_frame"]["timestamp"] = header->timestamp;
            metadata["last_frame"]["id"] = frame->getFrameId();
            metadata["last_frame"]["sequenceNumber"] = header->sequenceNumber;

            // Get frame metadata if any
            const FrameMetadata &frameMetadata = frame->getMetadata();

            json frameMetadataJson = {
                {"device_id", frameMetadata.deviceId},
                {"exposure_time_ms", frameMetadata.exposureTimeMs},
                {"frame_number", frameMetadata.frameNumber},
                {"processed", frameMetadata.hasBeenProcessed},
                {"calibration_data", frameMetadata.hasCalibrationData},
                {"segmentation_data", frameMetadata.hasSegmentationData},
                {"signal_to_noise_ratio", frameMetadata.signalToNoiseRatio},
                {"signal_strength", frameMetadata.signalStrength},
                {"confidence_score", frameMetadata.confidenceScore}
            };

            // Add position if available
            if (!frameMetadata.probePosition.empty()) {
                frameMetadataJson["probe_position"] = frameMetadata.probePosition;
            }

            // Add orientation if available
            if (!frameMetadata.probeOrientation.empty()) {
                frameMetadataJson["probe_orientation"] = frameMetadata.probeOrientation;
            }

            // Add attributes
            for (const auto &[key, value]: frameMetadata.attributes) {
                frameMetadataJson["attributes"][key] = value;
            }

            metadata["last_frame"]["metadata"] = frameMetadataJson;

            // Update the metadata
            impl_->updateMetadata(metadata);
        }

        // Update the write index and frame count
        uint64_t newWriteIndex = writeIndex + 1;
        impl_->controlBlock->writeIndex = newWriteIndex;
        impl_->controlBlock->totalFramesWritten++;

        // Update timestamp
        impl_->controlBlock->lastWriteTime = std::chrono::steady_clock::now().time_since_epoch().count();

        // Update max frame size
        if (frame->getDataSize() > impl_->controlBlock->maxFrameSize) {
            impl_->controlBlock->maxFrameSize = frame->getDataSize();
        }

        // Increase the frame count if we haven't wrapped around yet
        uint64_t readIndex = impl_->controlBlock->readIndex;
        if (newWriteIndex > readIndex) {
            impl_->controlBlock->frameCount = newWriteIndex - readIndex;
        } else {
            impl_->controlBlock->frameCount = impl_->maxFrames;
        }

        // Calculate write latency
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();

        // Update statistics
        {
            std::lock_guard<std::mutex> lock(statsMutex_);
            stats_.totalFramesWritten++;
            stats_.writeLatencyNsAvg = (stats_.writeLatencyNsAvg * (stats_.totalFramesWritten - 1) +
                                        duration) / stats_.totalFramesWritten;
            stats_.maxWriteLatencyNs = std::max(stats_.maxWriteLatencyNs, static_cast<uint64_t>(duration));
            stats_.averageFrameSize = (stats_.averageFrameSize * (stats_.totalFramesWritten - 1) +
                                       frame->getDataSize()) / stats_.totalFramesWritten;
            stats_.peakMemoryUsage = std::max(stats_.peakMemoryUsage, impl_->controlBlock->frameCount *
                                                                      (sizeof(FrameHeader) + static_cast<size_t>(stats_.
                                                                           averageFrameSize)));
        }

        return Status::OK;
    }

    SharedMemory::Status SharedMemory::readLatestFrame(std::shared_ptr<Frame> &frame) {
        if (!isInitialized_ || !impl_->controlBlock) {
            return Status::NOT_INITIALIZED;
        }

        // Start time for performance tracking
        auto startTime = std::chrono::high_resolution_clock::now();

        // Get the current write and read indices
        uint64_t writeIndex = impl_->controlBlock->writeIndex;
        uint64_t readIndex = impl_->controlBlock->readIndex;

        if (writeIndex == 0 || writeIndex <= readIndex) {
            return Status::BUFFER_EMPTY;
        }

        // Get the latest frame (the one just before the write index)
        uint64_t latestIndex = writeIndex - 1;

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

        // Check if this is a zero-copy frame that's already in shared memory
        if (header->flags & 0x01) {
            // This is a zero-copy frame - create a reference to the original
            // memory location instead of this shared memory
            // TODO: Implement proper zero-copy for frames already in shared memory
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

        // Create a mapped frame
        frame = Frame::createMapped(
            config_.name,
            static_cast<uint8_t *>(dataPtr) - static_cast<uint8_t *>(impl_->mapping),
            header->dataSize,
            header->width,
            header->height,
            header->bytesPerPixel,
            format);

        if (!frame) {
            return Status::INTERNAL_ERROR;
        }

        // Set the frame ID and timestamp
        frame->setFrameId(header->frameId);
        auto timestamp = std::chrono::system_clock::time_point(
            std::chrono::nanoseconds(header->timestamp));
        frame->setTimestamp(timestamp);

        // Load metadata if available
        if (config_.enableMetadata) {
            auto metadata = impl_->readMetadata();
            if (metadata.contains("last_frame") && metadata["last_frame"].contains("metadata")) {
                auto &frameMetadataJson = metadata["last_frame"]["metadata"];

                // Get a mutable reference to the frame metadata
                FrameMetadata &frameMetadata = frame->getMetadataMutable();

                // Copy metadata fields
                if (frameMetadataJson.contains("device_id")) {
                    frameMetadata.deviceId = frameMetadataJson["device_id"];
                }

                if (frameMetadataJson.contains("exposure_time_ms")) {
                    frameMetadata.exposureTimeMs = frameMetadataJson["exposure_time_ms"];
                }

                if (frameMetadataJson.contains("frame_number")) {
                    frameMetadata.frameNumber = frameMetadataJson["frame_number"];
                }

                if (frameMetadataJson.contains("processed")) {
                    frameMetadata.hasBeenProcessed = frameMetadataJson["processed"];
                }

                if (frameMetadataJson.contains("calibration_data")) {
                    frameMetadata.hasCalibrationData = frameMetadataJson["calibration_data"];
                }

                if (frameMetadataJson.contains("segmentation_data")) {
                    frameMetadata.hasSegmentationData = frameMetadataJson["segmentation_data"];
                }

                if (frameMetadataJson.contains("signal_to_noise_ratio")) {
                    frameMetadata.signalToNoiseRatio = frameMetadataJson["signal_to_noise_ratio"];
                }

                if (frameMetadataJson.contains("signal_strength")) {
                    frameMetadata.signalStrength = frameMetadataJson["signal_strength"];
                }

                if (frameMetadataJson.contains("confidence_score")) {
                    frameMetadata.confidenceScore = frameMetadataJson["confidence_score"];
                }

                // Copy array data
                if (frameMetadataJson.contains("probe_position")) {
                    frameMetadata.probePosition = frameMetadataJson["probe_position"]
                            .get<std::vector<float> >();
                }

                if (frameMetadataJson.contains("probe_orientation")) {
                    frameMetadata.probeOrientation = frameMetadataJson["probe_orientation"]
                            .get<std::vector<float> >();
                }

                // Copy attributes
                if (frameMetadataJson.contains("attributes") &&
                    frameMetadataJson["attributes"].is_object()) {
                    for (auto &[key, value]: frameMetadataJson["attributes"].items()) {
                        frameMetadata.attributes[key] = value;
                    }
                }
            }
        }

        // Update timestamp
        impl_->controlBlock->lastReadTime = std::chrono::steady_clock::now().time_since_epoch().count();

        // Calculate read latency
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();

        // Update statistics
        {
            std::lock_guard<std::mutex> lock(statsMutex_);
            stats_.totalFramesRead++;
            stats_.readLatencyNsAvg = (stats_.readLatencyNsAvg * (stats_.totalFramesRead - 1) +
                                       duration) / stats_.totalFramesRead;
            stats_.maxReadLatencyNs = std::max(stats_.maxReadLatencyNs, static_cast<uint64_t>(duration));
        }

        return Status::OK;
    }

    SharedMemory::Status SharedMemory::readNextFrame(std::shared_ptr<Frame> &frame,
                                                     unsigned int waitMilliseconds) {
        if (!isInitialized_ || !impl_->controlBlock) {
            return Status::NOT_INITIALIZED;
        }

        // Start time for performance tracking
        auto startTime = std::chrono::high_resolution_clock::now();

        // Get the current read index
        uint64_t readIndex = impl_->controlBlock->readIndex;
        uint64_t writeIndex = impl_->controlBlock->writeIndex;

        // Check if there are any frames available
        if (readIndex >= writeIndex) {
            if (waitMilliseconds == 0) {
                return Status::BUFFER_EMPTY;
            }

            // Wait for a new frame
            auto endTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitMilliseconds);

            while (readIndex >= writeIndex && std::chrono::steady_clock::now() < endTime) {
                // Sleep for a short time
                std::this_thread::sleep_for(std::chrono::milliseconds(1));

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

        // Check if this is a zero-copy frame that's already in shared memory
        if (header->flags & 0x01) {
            // This is a zero-copy frame - create a reference to the original
            // memory location instead of this shared memory
            // TODO: Implement proper zero-copy for frames already in shared memory
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

        // Create a frame that references the shared memory
        frame = Frame::createMapped(
            config_.name,
            static_cast<uint8_t *>(dataPtr) - static_cast<uint8_t *>(impl_->mapping),
            header->dataSize,
            header->width,
            header->height,
            header->bytesPerPixel,
            format);

        if (!frame) {
            return Status::INTERNAL_ERROR;
        }

        // Set the frame ID and timestamp
        frame->setFrameId(header->frameId);
        auto timestamp = std::chrono::system_clock::time_point(
            std::chrono::nanoseconds(header->timestamp));
        frame->setTimestamp(timestamp);

        // Load metadata if available
        if (config_.enableMetadata) {
            auto metadata = impl_->readMetadata();
            if (metadata.contains("last_frame") && metadata["last_frame"].contains("metadata")) {
                auto &frameMetadataJson = metadata["last_frame"]["metadata"];

                // Get a mutable reference to the frame metadata
                FrameMetadata &frameMetadata = frame->getMetadataMutable();

                // Copy metadata fields
                if (frameMetadataJson.contains("device_id")) {
                    frameMetadata.deviceId = frameMetadataJson["device_id"];
                }

                if (frameMetadataJson.contains("exposure_time_ms")) {
                    frameMetadata.exposureTimeMs = frameMetadataJson["exposure_time_ms"];
                }

                if (frameMetadataJson.contains("frame_number")) {
                    frameMetadata.frameNumber = frameMetadataJson["frame_number"];
                }

                if (frameMetadataJson.contains("processed")) {
                    frameMetadata.hasBeenProcessed = frameMetadataJson["processed"];
                }

                if (frameMetadataJson.contains("calibration_data")) {
                    frameMetadata.hasCalibrationData = frameMetadataJson["calibration_data"];
                }

                if (frameMetadataJson.contains("segmentation_data")) {
                    frameMetadata.hasSegmentationData = frameMetadataJson["segmentation_data"];
                }
            }
        }

        // Update the read index
        impl_->controlBlock->readIndex = readIndex + 1;

        // Update timestamp
        impl_->controlBlock->lastReadTime = std::chrono::steady_clock::now().time_since_epoch().count();

        // Update the frame count
        if (impl_->controlBlock->frameCount > 0) {
            --impl_->controlBlock->frameCount;
        }

        // Update statistics
        impl_->controlBlock->totalFramesRead++;

        // Calculate read latency
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();

        // Update local statistics
        {
            std::lock_guard<std::mutex> lock(statsMutex_);
            stats_.totalFramesRead++;
            stats_.readLatencyNsAvg = (stats_.readLatencyNsAvg * (stats_.totalFramesRead - 1) +
                                       duration) / stats_.totalFramesRead;
            stats_.maxReadLatencyNs = std::max(stats_.maxReadLatencyNs, static_cast<uint64_t>(duration));
        }

        return Status::OK;
    }

    SharedMemory::Status SharedMemory::registerFrameCallback(std::function<void(std::shared_ptr<Frame>)> callback) {
        std::unique_lock<std::mutex> lock(callbackMutex_);

        // Store the callback
        frameCallback_ = std::move(callback);

        // Start the notification thread if we're initialized
        if (isInitialized_ && !config_.create && !callbackThread_.joinable()) {
            stopCallbackThread_ = false;
            callbackThread_ = std::thread(&SharedMemory::notificationThread, this);

            // Set thread priority and affinity if configured
            if (threadPriority_ != 0) {
                setThreadPriority(threadPriority_);
            }

            if (threadAffinity_ >= 0) {
                setThreadAffinity(threadAffinity_);
            }
        }

        return Status::OK;
    }

    SharedMemory::Status SharedMemory::unregisterFrameCallback() {
        std::unique_lock<std::mutex> lock(callbackMutex_);

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

    SharedMemory::Status SharedMemory::setThreadAffinity(int cpuCore) {
        threadAffinity_ = cpuCore;

        // If the thread is already running, set its affinity now
        if (callbackThread_.joinable()) {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(cpuCore, &cpuset);

            // Get native thread handle
            pthread_t nativeHandle = callbackThread_.native_handle();

            // Set CPU affinity
            int result = pthread_setaffinity_np(nativeHandle, sizeof(cpu_set_t), &cpuset);

            if (result != 0) {
                std::cerr << "Failed to set thread affinity: " << result << std::endl;
                return Status::INTERNAL_ERROR;
            }
        }

        return Status::OK;
    }

    SharedMemory::Status SharedMemory::setThreadPriority(int priority) {
        threadPriority_ = priority;

        // If the thread is already running, set its priority now
        if (callbackThread_.joinable()) {
            // Set thread priority using POSIX APIs
            sched_param param;
            int policy;

            if (priority > 0) {
                policy = SCHED_RR; // Round-robin real-time scheduler
                param.sched_priority = priority;
            } else {
                policy = SCHED_OTHER; // Standard scheduler
                param.sched_priority = 0; // Ignored for SCHED_OTHER
            }

            // Get native thread handle
            pthread_t nativeHandle = callbackThread_.native_handle();

            // Set scheduler policy and priority
            int result = pthread_setschedparam(nativeHandle, policy, &param);

            if (result != 0) {
                std::cerr << "Failed to set thread priority: " << result << std::endl;
                return Status::INTERNAL_ERROR;
            }
        }

        return Status::OK;
    }

    SharedMemory::Status SharedMemory::lockMemory() {
        if (!isInitialized_ || !impl_->mapping) {
            return Status::NOT_INITIALIZED;
        }

        // Lock the memory in RAM to prevent swapping
        if (mlock(impl_->mapping, impl_->size) != 0) {
            std::cerr << "Failed to lock memory: " << strerror(errno) << std::endl;
            return Status::PERMISSION_DENIED;
        }

        return Status::OK;
    }

    SharedMemory::Status SharedMemory::unlockMemory() {
        if (!isInitialized_ || !impl_->mapping) {
            return Status::NOT_INITIALIZED;
        }

        // Unlock the memory
        if (munlock(impl_->mapping, impl_->size) != 0) {
            std::cerr << "Failed to unlock memory: " << strerror(errno) << std::endl;
            return Status::PERMISSION_DENIED;
        }

        return Status::OK;
    }

    SharedMemory::Statistics SharedMemory::getStatistics() {
        // Get statistics from the shared control block
        if (isInitialized_ && impl_->controlBlock) {
            std::lock_guard<std::mutex> lock(statsMutex_);

            // Update from control block
            stats_.totalFramesWritten = impl_->controlBlock->totalFramesWritten;
            stats_.totalFramesRead = impl_->controlBlock->totalFramesRead;
            stats_.droppedFrames = impl_->controlBlock->droppedFrames;

            return stats_;
        }

        // Return local stats if not initialized
        std::lock_guard<std::mutex> lock(statsMutex_);
        return stats_;
    }

    void SharedMemory::resetStatistics() {
        std::lock_guard<std::mutex> lock(statsMutex_);

        stats_ = {};

        // Reset control block stats if initialized
        if (isInitialized_ && impl_->controlBlock && config_.create) {
            impl_->controlBlock->droppedFrames = 0;
        }
    }

    std::string SharedMemory::getName() const {
        return config_.name;
    }

    size_t SharedMemory::getSize() const {
        return config_.size;
    }

    SharedMemoryType SharedMemory::getType() const {
        return config_.type;
    }

    size_t SharedMemory::getMaxFrames() const {
        return impl_->maxFrames;
    }

    size_t SharedMemory::getCurrentFrameCount() const {
        if (!isInitialized_ || !impl_->controlBlock) {
            return 0;
        }

        return impl_->controlBlock->frameCount;
    }

    bool SharedMemory::isBufferFull() const {
        if (!isInitialized_ || !impl_->controlBlock) {
            return false;
        }

        return impl_->controlBlock->frameCount >= impl_->maxFrames;
    }

    bool SharedMemory::isBufferEmpty() const {
        if (!isInitialized_ || !impl_->controlBlock) {
            return true;
        }

        return impl_->controlBlock->frameCount == 0;
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
            uint64_t writeIndex = impl_->controlBlock->writeIndex;
            uint64_t readIndex = impl_->controlBlock->readIndex;

            // Check if there are new frames
            if (writeIndex > lastReadIndex) {
                // Process all new frames
                for (uint64_t i = lastReadIndex; i < writeIndex; ++i) {
                    // Get the frame
                    std::shared_ptr<Frame> frame;

                    if (readNextFrame(frame, 0) == Status::OK && frame) {
                        // Call the callback
                        std::unique_lock<std::mutex> lock(callbackMutex_);
                        if (frameCallback_) {
                            frameCallback_(frame);
                        }
                    }
                }

                // Update the last read index
                lastReadIndex = writeIndex;
            }

            // Sleep for a short time
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // SharedMemoryManager implementation
    SharedMemoryManager& SharedMemoryManager::getInstance() {
        static SharedMemoryManager instance;
        return instance;
    }

    std::shared_ptr<SharedMemory> SharedMemoryManager::createOrGet(const std::string& name,
                                                             size_t size,
                                                             SharedMemoryType type) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Check if a shared memory with this name already exists
        auto it = sharedMemories_.find(name);
        if (it != sharedMemories_.end()) {
            return it->second;
        }

        // Create configuration
        SharedMemory::Config config;
        config.name = name;
        config.size = size;
        config.type = type;
        config.create = true;  // Create as server

        // Create shared memory
        auto shm = std::make_shared<SharedMemory>(config);

        // Initialize it
        if (shm->initialize() != SharedMemory::Status::OK) {
            return nullptr;
        }

        // Store and return it
        sharedMemories_[name] = shm;
        return shm;
    }

    bool SharedMemoryManager::releaseSharedMemory(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = sharedMemories_.find(name);
        if (it != sharedMemories_.end()) {
            // Remove it from the map
            sharedMemories_.erase(it);
            return true;
        }

        return false;
    }

    void SharedMemoryManager::releaseAll() {
        std::lock_guard<std::mutex> lock(mutex_);

        // Clear all shared memories
        sharedMemories_.clear();
    }

    SharedMemoryManager::~SharedMemoryManager() {
        releaseAll();
    }
}
