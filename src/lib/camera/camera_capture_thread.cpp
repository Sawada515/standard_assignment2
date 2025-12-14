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
    capture_thread_ = std::thread(&CameraCaptureThread::captureLoop, this);
    
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
        V4L2Capture::Frame frame = frame_queue_.front();
        frame_queue_.pop();
        
        // メモリ確保してあるデータを解放
        if (frame.data) {
            delete[] static_cast<uint8_t*>(frame.data);
        }
    }

    LOG_I("Camera capture thread stopped");
}

bool CameraCaptureThread::getframe(V4L2Capture::Frame& frame)
{
    std::unique_lock<std::mutex> lock(mutex_);

    // データが来るまで待つ（最大100ms）
    bool result = cond_var_.wait_for(lock, std::chrono::milliseconds(100), [this] {
        return !frame_queue_.empty();
    });

    if (result && !frame_queue_.empty()) {
        frame = frame_queue_.front();
        frame_queue_.pop();
        
        // 【注意】
        // ここで frame.data には new されたポインタが入っています。
        // 呼び出し元で使い終わったら必ず delete[] frame.data してください。
        
        return true;
    }

    return false;
}

void CameraCaptureThread::captureLoop(void)
{
    LOG_I("Capture loop started");

    while (running_) {
        V4L2Capture::Frame temp_frame;
        
        // カメラからフレームを取得
        if (camera_.read_frame(temp_frame)) {
            
            // 【重要】データのディープコピー（Deep Copy）
            // read_frameで得られるポインタは一時的なものなので、
            // 新しいメモリ領域を作ってコピーしておく必要がある。
            
            V4L2Capture::Frame saved_frame;
            saved_frame.width = temp_frame.width;
            saved_frame.height = temp_frame.height;
            saved_frame.length = temp_frame.length;
            
            // 新規メモリ確保
            saved_frame.data = new uint8_t[temp_frame.length];
            
            if (saved_frame.data) {
                std::memcpy(saved_frame.data, temp_frame.data, temp_frame.length);

                // キューへの追加（排他制御）
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    
                    // キューがあふれないように制限（最新の5フレームだけ保持）
                    // これがないと処理が追いつかない場合にメモリを食いつぶす
                    if (frame_queue_.size() > 5) {
                        V4L2Capture::Frame old_frame = frame_queue_.front();
                        frame_queue_.pop();
                        // 古いフレームのメモリを解放
                        delete[] static_cast<uint8_t*>(old_frame.data);

                        LOG_W("Frame queue full, dropped old frame");
                    }

                    frame_queue_.push(saved_frame);
                }
                
                // 待っているスレッド（getframe）を起こす
                cond_var_.notify_one();
            } else {
                LOG_E("Failed to allocate memory for frame copy");
            }
        } else {
            // フレームが取れなかった場合やエラー時は少し待機
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    LOG_I("Capture loop finished");
}
