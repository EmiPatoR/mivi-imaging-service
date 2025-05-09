#include "frame/frame.h"
#include <cstring>
#include <map>

namespace medical {
namespace imaging {

/**
 * @brief Private implementation of Frame
 */
struct Frame::Impl {
    void* data;                 // Pointer to raw frame data
    size_t dataSize;            // Size of the data in bytes
    int width;                  // Frame width in pixels
    int height;                 // Frame height in pixels
    int bytesPerPixel;          // Number of bytes per pixel
    std::string format;         // Pixel format string
    uint64_t frameId;           // Unique frame ID
    std::chrono::system_clock::time_point timestamp; // Frame timestamp
    bool ownsData;              // Whether this frame owns the data buffer
    std::map<std::string, std::string> metadata; // Additional metadata
    
    // Constructor
    Impl() : 
        data(nullptr), 
        dataSize(0), 
        width(0), 
        height(0), 
        bytesPerPixel(0), 
        format(""), 
        frameId(0), 
        timestamp(std::chrono::system_clock::now()), 
        ownsData(false) {}
    
    // Destructor
    ~Impl() {
        if (ownsData && data) {
            free(data);
            data = nullptr;
        }
    }
};

Frame::Frame() : impl_(std::make_unique<Impl>()) {}

Frame::~Frame() {
    {
        if (onDestroyCallback_) {
            onDestroyCallback_();
        }
    }
}


std::shared_ptr<Frame> Frame::create(
    int width, int height, int bytesPerPixel, const std::string& format) {
    
    // Calculate required buffer size
    size_t dataSize = static_cast<size_t>(width) * height * bytesPerPixel;
    
    // Create new frame
    std::shared_ptr<Frame> frame = std::shared_ptr<Frame>(new Frame());
    
    // Allocate memory
    frame->impl_->data = malloc(dataSize);
    if (!frame->impl_->data) {
        return nullptr;
    }
    
    // Initialize members
    frame->impl_->dataSize = dataSize;
    frame->impl_->width = width;
    frame->impl_->height = height;
    frame->impl_->bytesPerPixel = bytesPerPixel;
    frame->impl_->format = format;
    frame->impl_->ownsData = true;
    frame->impl_->timestamp = std::chrono::system_clock::now();
    
    return frame;
}

    std::shared_ptr<Frame> Frame::createWithExternalData(
        void* data, size_t size, int width, int height, int bytesPerPixel,
        const std::string& format, bool ownsData) {

    if (!data || size == 0) {
        return nullptr;
    }

    // Create new frame
    std::shared_ptr<Frame> frame = std::shared_ptr<Frame>(new Frame());

    if (ownsData) {
        // Make a copy of the data
        frame->impl_->data = malloc(size);
        if (!frame->impl_->data) {
            return nullptr;
        }

        std::memcpy(frame->impl_->data, data, size);
    } else {
        // Use the provided data directly (zero-copy)
        frame->impl_->data = data;
    }

    // Initialize members
    frame->impl_->dataSize = size;
    frame->impl_->width = width;
    frame->impl_->height = height;
    frame->impl_->bytesPerPixel = bytesPerPixel;
    frame->impl_->format = format;
    frame->impl_->ownsData = ownsData;
    frame->impl_->timestamp = std::chrono::system_clock::now();

    return frame;
}

void* Frame::getData() const {
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

std::shared_ptr<Frame> Frame::clone() const {
    // Create a new frame with a copy of the data
    auto newFrame = Frame::create(
        impl_->width, 
        impl_->height, 
        impl_->bytesPerPixel, 
        impl_->format);
    
    if (!newFrame) {
        return nullptr;
    }
    
    // Copy data
    std::memcpy(newFrame->impl_->data, impl_->data, impl_->dataSize);
    
    // Copy metadata
    newFrame->impl_->frameId = impl_->frameId;
    newFrame->impl_->timestamp = impl_->timestamp;
    newFrame->impl_->metadata = impl_->metadata;
    
    return newFrame;
}

void Frame::setMetadata(const std::string& key, const std::string& value) {
    impl_->metadata[key] = value;
}

std::string Frame::getMetadata(const std::string& key) const {
    auto it = impl_->metadata.find(key);
    if (it != impl_->metadata.end()) {
        return it->second;
    }
    
    return "";
}

} // namespace imaging
} // namespace medical