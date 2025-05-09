#include "frame/frame_processor.h"
#include <chrono>
#include <iostream>
#include <map>


namespace medical::imaging {
    FrameProcessor::FrameProcessor(const Config &config)
        : config_(config), running_(false) {
    }

    FrameProcessor::~FrameProcessor() {
        stop();
    }

    std::shared_ptr<Frame> FrameProcessor::processFrame(std::shared_ptr<Frame> frame) {
        if (!frame) {
            return nullptr;
        }

        // Record start time for performance measurement
        auto startTime = std::chrono::high_resolution_clock::now();

        // Perform processing operations
        // This is where you would add thyroid segmentation, probe tracking, etc.

        // For now, just add a metadata field indicating the frame was processed
        frame->setMetadata("processed", "true");
        frame->setMetadata("segmentation_enabled", config_.enableSegmentation ? "true" : "false");
        frame->setMetadata("calibration_enabled", config_.enableCalibration ? "true" : "false");

        // Calculate processing time
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

        // Update statistics
        ++frameCount_;
        processingTimeTotal_ += duration.count();

        // Add processing time to metadata
        frame->setMetadata("processing_time_us", std::to_string(duration.count()));

        return frame;
    }

    void FrameProcessor::setFrameCallback(const std::function<void(std::shared_ptr<Frame>)> &callback) {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        frameCallback_ = callback;
    }

    void FrameProcessor::start() {
        if (running_) {
            return;
        }

        running_ = true;

        // Initialize statistics
        frameCount_ = 0;
        droppedFrames_ = 0;
        processingTimeTotal_ = 0;

        // Clear the queue
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            std::queue<std::shared_ptr<Frame> > empty;
            std::swap(frameQueue_, empty);
        }

        // Create processing threads
        threads_.clear();
        for (int i = 0; i < config_.numThreads; ++i) {
            threads_.emplace_back(&FrameProcessor::processingThread, this);
        }
    }

    void FrameProcessor::stop() {
        if (!running_) {
            return;
        }

        running_ = false;

        // Wake up all threads
        frameCondition_.notify_all();

        // Wait for them to finish
        for (auto &thread: threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        threads_.clear();
    }

    bool FrameProcessor::isRunning() const {
        return running_;
    }

    void FrameProcessor::queueFrame(const std::shared_ptr<Frame> &frame) {
        if (!running_ || !frame) {
            return;
        }

        bool queueFull = false; {
            std::lock_guard lock(queueMutex_);

            // Check if the queue is full
            if (frameQueue_.size() >= config_.maxQueueSize) {
                // Arbitrary max queue size
                queueFull = true;
                ++droppedFrames_;
                std::cout << "CCC" << std::endl;
            } else {
                frameQueue_.push(frame);
            }
        }

        if (!queueFull) {
            // Notify one thread that a new frame is available
            frameCondition_.notify_one();
        }
    }


    void FrameProcessor::processingThread() {
        while (running_) {
            std::shared_ptr<Frame> frame; {
                std::unique_lock<std::mutex> lock(queueMutex_);

                // Wait for a frame to be available
                frameCondition_.wait(lock, [this] {
                    return !running_ || !frameQueue_.empty();
                });

                // Check if we're shutting down
                if (!running_) {
                    break;
                }

                // Get the next frame from the queue
                if (!frameQueue_.empty()) {
                    frame = frameQueue_.front();
                    frameQueue_.pop();
                }
            }

            // Process the frame if we got one
            if (frame) {
                auto processedFrame = processFrame(frame);

                // Call the callback with the processed frame
                if (processedFrame) {
                    std::lock_guard<std::mutex> lock(callbackMutex_);
                    if (frameCallback_) {
                        frameCallback_(processedFrame);
                    }
                }
            }
        }
    }

    std::map<std::string, std::string> FrameProcessor::getStatistics() const {
        std::map<std::string, std::string> stats;

        stats["frame_count"] = std::to_string(frameCount_);
        stats["dropped_frames"] = std::to_string(droppedFrames_);

        // Calculate average processing time in milliseconds
        if (frameCount_ > 0) {
            double avgTimeMs = static_cast<double>(processingTimeTotal_) / frameCount_ / 1000.0;
            stats["avg_processing_time_ms"] = std::to_string(avgTimeMs);
        } else {
            stats["avg_processing_time_ms"] = "0.0";
        }

        stats["running"] = running_ ? "true" : "false";
        stats["thread_count"] = std::to_string(threads_.size());
        stats["queue_size"] = std::to_string(frameQueue_.size());

        return stats;
    }
} // namespace medical::imaging
