#pragma once

#pragma once

#include <memory>
#include <functional>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <map>

#include "frame/frame.h"

namespace medical {
namespace imaging {

/**
 * @class FrameProcessor
 * @brief Processes video frames, applying segmentation and other operations
 * 
 * This class handles the processing of ultrasound frames, including
 * thyroid segmentation, probe tracking, and other analysis operations.
 */
class FrameProcessor {
public:
    /**
     * @brief Configuration for the frame processor
     */
    struct Config {
        bool enableSegmentation;        // Whether to enable segmentation
        bool enableCalibration;         // Whether to enable probe calibration
        int numThreads;                 // Number of processing threads
        unsigned int maxQueueSize;      // Number of frames in the Queue
        
        // Constructor with defaults
        Config() : 
            enableSegmentation(true),
            enableCalibration(true),
            numThreads(2),
            maxQueueSize(120){}
    };
    
    /**
     * @brief Constructor
     * @param config Processing configuration
     */
    explicit FrameProcessor(const Config& config = Config());
    
    /**
     * @brief Destructor
     */
    ~FrameProcessor();
    
    /**
     * @brief Process a frame
     * @param frame Frame to process
     * @return Processed frame
     */
    std::shared_ptr<Frame> processFrame(std::shared_ptr<Frame> frame);
    
    /**
     * @brief Set a callback for processed frames
     * @param callback Function to call with each processed frame
     */
    void setFrameCallback(const std::function<void(std::shared_ptr<Frame>)> &callback);
    
    /**
     * @brief Start the processing threads
     */
    void start();
    
    /**
     * @brief Stop the processing threads
     */
    void stop();
    
    /**
     * @brief Check if the processor is running
     * @return true if running, false otherwise
     */
    bool isRunning() const;
    
    /**
     * @brief Queue a frame for processing
     * @param frame Frame to process
     */
    void queueFrame(const std::shared_ptr<Frame>& frame);
    
    /**
     * @brief Get statistics about the processor
     * @return Map of statistic name to value
     */
    std::map<std::string, std::string> getStatistics() const;

private:
    // Processing thread function
    void processingThread();
    
    // Configuration
    Config config_;
    
    // Processing state
    std::atomic<bool> running_;
    std::vector<std::thread> threads_;
    std::queue<std::shared_ptr<Frame>> frameQueue_;
    mutable std::mutex queueMutex_;
    std::condition_variable frameCondition_;
    
    // Callback
    std::function<void(std::shared_ptr<Frame>)> frameCallback_;
    std::mutex callbackMutex_;
    
    // Statistics
    std::atomic<uint64_t> frameCount_{};
    std::atomic<uint64_t> droppedFrames_{};
    std::atomic<uint64_t> processingTimeTotal_{};
    mutable std::mutex statsMutex_;
};

} // namespace imaging
} // namespace medical