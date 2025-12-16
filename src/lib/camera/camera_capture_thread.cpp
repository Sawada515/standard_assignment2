/**
 * @file    camera_capture_thread.cpp
 * @brief   Webカメラから画像データを取得するスレッドの実装
 * @author  sawada souta
 * @version 0.1
 * @date    2025-12-14
 */

#include <cstring>
#include <chrono>
#include <string>

#include "camera/camera_capture_thread.hpp"
#include "logger/logger.hpp"

#define MAX_QUEUE_SIZE 2

CameraCaptureThread::CameraCaptureThread(uint32_t width, uint32_t height, const std::string& device)
    : capture_thread_(),
      mutex_(),
      cond_var_(),
      frame_queue_(),
      running_(false),
      device_name_(device),
      camera_(width, height)
{
    LOG_I("CameraCaptureThread constructor called with device: %s", device_name_.c_str());
}

CameraCaptureThread::~CameraCaptureThread()
{
    stop();

    LOG_I("CameraCaptureThread destructor called");
}

void CameraCaptureThread::start(void)
{
    if (running_) {
        LOG_W("Thread is already running");

        return;
    }

    // 保存しておいたパスを使ってデバイスを開く
    if (!camera_.open_device(device_name_)) {
        LOG_E("Failed to open camera device: %s", device_name_.c_str());

        return;
    }

    if (!camera_.start_stream()) {
        LOG_E("Failed to start camera stream");

        camera_.close_device();
        return;
    }

    running_ = true;
    capture_thread_ = std::thread(&CameraCaptureThread::capture_loop, this);
    
    LOG_I("Camera capture thread started");
}

void CameraCaptureThread::stop(void)
{
    if (!running_) {
        return;
    }

    running_ = false;

    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }

    camera_.stop_stream();
    camera_.close_device();

    // キューに残っているデータをクリア（メモリリーク防止）
    std::lock_guard<std::mutex> lock(mutex_);
    while (!frame_queue_.empty()) {
        V4L2Capture::Frame frame = std::move(frame_queue_.front());
        frame_queue_.pop();
    }

    LOG_I("Camera capture thread stopped");
}

bool CameraCaptureThread::get_frame(V4L2Capture::Frame& frame)
{
    std::unique_lock<std::mutex> lock(mutex_);

    cond_var_.wait(lock, [this]() {
        return !frame_queue_.empty() || !running_;
    });

    if (!running_ && frame_queue_.empty()) {
        return false;
    }

    frame = std::move(frame_queue_.front());
    frame_queue_.pop();

    return true;
}

void CameraCaptureThread::capture_loop(void)
{
    LOG_I("Capture loop started");

    while (running_) {
        V4L2Capture::Frame frame;

        if (camera_.get_frame(frame)) {
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (frame_queue_.size() >= MAX_QUEUE_SIZE) {
                    frame_queue_.pop();
                }

                frame_queue_.push(std::move(frame));
            }

            cond_var_.notify_one();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    LOG_I("Capture loop finished");
}
