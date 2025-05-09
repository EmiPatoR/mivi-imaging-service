#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
#include <chrono>
#include <functional>
#include <string>

namespace medical {
namespace imaging {

/**
 * @class Frame
 * @brief Represents a captured ultrasound frame with metadata
 * 
 * This class encapsulates a single frame of ultrasound video data along
 * with relevant metadata such as timestamp, dimensions, and format.
 * It provides methods to access the raw data and metadata.
 */
class Frame {
public:
    /**
     * @brief Create a new frame with allocated buffer
     * @param width Frame width in pixels
     * @param height Frame height in pixels
     * @param bytesPerPixel Number of bytes per pixel
     * @param format String identifier for the pixel format
     * @return Shared pointer to the created frame
     */
    static std::shared_ptr<Frame> create(
        int width, int height, int bytesPerPixel, const std::string& format);
    
    /**
     * @brief Create a frame that wraps existing data (zero-copy when possible)
     * @param data Pointer to the raw frame data
     * @param size Size of the data in bytes
     * @param width Frame width in pixels
     * @param height Frame height in pixels
     * @param bytesPerPixel Number of bytes per pixel
     * @param format String identifier for the pixel format
     * @param ownsData Whether this frame should take ownership of the data
     * @return Shared pointer to the created frame
     */
    static std::shared_ptr<Frame> createWithExternalData(
        void* data, size_t size, int width, int height, int bytesPerPixel, 
        const std::string& format, bool ownsData = false);
    
    /**
     * @brief Destructor
     */
    ~Frame();

    void setOnDestroy(std::function<void()> callback) {
        onDestroyCallback_ = std::move(callback);
    };
    
    /**
     * @brief Get a pointer to the raw frame data
     * @return Pointer to the frame data
     */
    void* getData() const;
    
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
     * @brief Deep copy the frame
     * @return New frame with copied data
     */
    std::shared_ptr<Frame> clone() const;
    
    /**
     * @brief Set metadata associated with this frame
     * @param key Metadata key
     * @param value Metadata value
     */
    void setMetadata(const std::string& key, const std::string& value);
    
    /**
     * @brief Get metadata associated with this frame
     * @param key Metadata key
     * @return Metadata value, or empty string if not found
     */
    std::string getMetadata(const std::string& key) const;

private:
    // Private implementation to hide details
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::function<void()> onDestroyCallback_;
    
    // Private constructor, use factory methods instead
    Frame();
};

} // namespace imaging
} // namespace medical