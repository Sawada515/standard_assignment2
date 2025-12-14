/**
 * @file    udp_sender_thread.cpp
 * @brief   画像データ送信専用のスレッドクラス実装
 * @author  sawada souta
 * @date    2025-12-14
 */

#include <cstring>

#include "network/udp_sender_thread.hpp"
#include "logger/logger.hpp"

UDPSenderThread::UDPSenderThread(const std::string& ip, uint16_t port)
    : sender_(ip, port),
      send_thread_(),
      mutex_(),
      cond_var_(),
      send_queue_(),
      running_(false)
{
    LOG_I("UDPSenderThread initialized. Target: %s:%d", ip.c_str(), port);
}

UDPSenderThread::~UDPSenderThread()
{
    stop();
}

void UDPSenderThread::start(void)
{
    if (running_) {
        return;
    }

    running_ = true;
    send_thread_ = std::thread(&UDPSenderThread::sendLoop, this);
    
    LOG_I("UDP sender thread started");
}

void UDPSenderThread::stop(void)
{
    if (!running_) {
        return;
    }

    running_ = false;
    cond_var_.notify_all(); // 待機中のスレッドを起こす

    if (send_thread_.joinable()) {
        send_thread_.join();
    }

    // キューに残っているデータのメモリを解放
    std::lock_guard<std::mutex> lock(mutex_);
    while (!send_queue_.empty()) {
        V4L2Capture::Frame frame = send_queue_.front();
        send_queue_.pop();
        if (frame.data) {
            delete[] static_cast<uint8_t*>(frame.data);
        }
    }

    LOG_I("UDP sender thread stopped");
}

void UDPSenderThread::enqueue(const V4L2Capture::Frame& frame)
{
    if (!running_) return;

    // データのディープコピーを作成
    // (呼び出し元がframeをすぐに解放しても大丈夫なようにする)
    V4L2Capture::Frame copied_frame = frame;
    
    // 新しいメモリ領域を確保
    copied_frame.data = new uint8_t[frame.length];
    if (!copied_frame.data) {
        LOG_E("Failed to allocate memory for UDP send queue");

        return;
    }
    
    // データをコピー
    std::memcpy(copied_frame.data, frame.data, frame.length);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // キューがあふれないように制限（例: 最大10フレーム待ちまで）
        // ネットワークが遅い場合にメモリを食いつぶさないための保護
        if (send_queue_.size() > 10) {
            V4L2Capture::Frame old_frame = send_queue_.front();
            send_queue_.pop();
            delete[] static_cast<uint8_t*>(old_frame.data); // 古いデータを捨てる

            LOG_W("UDP send queue full, dropped old frame");
        }

        send_queue_.push(copied_frame);
    }

    cond_var_.notify_one();
}

void UDPSenderThread::enqueue_vector(const std::vector<uint8_t>& vec_data)
{
    if (!running_) return;

    V4L2Capture::Frame frame;
    frame.width = 0;
    frame.height = 0;
    frame.length = vec_data.size();
    
    // 送信スレッド側で delete[] するために new で確保
    frame.data = new uint8_t[frame.length];
    
    if (frame.data) {
        std::memcpy(frame.data, vec_data.data(), frame.length);
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (send_queue_.size() > 10) {
                V4L2Capture::Frame old = send_queue_.front();
                send_queue_.pop();
                if (old.data) {
                    delete[] static_cast<uint8_t*>(old.data);
                }
            }
            send_queue_.push(frame);
        }
        cond_var_.notify_one();
    }
    else {
        LOG_E("Failed to allocate memory in enqueue_vector");
    }
}

void UDPSenderThread::sendLoop(void)
{
    while (running_) {
        V4L2Capture::Frame frame_to_send;
        frame_to_send.data = nullptr;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            
            // データが来るまで待機
            cond_var_.wait(lock, [this] {
                return !send_queue_.empty() || !running_;
            });

            if (!running_) break;

            if (!send_queue_.empty()) {
                frame_to_send = send_queue_.front();
                send_queue_.pop();
            }
        }

        // 送信処理（ロックの外で行うのが重要）
        if (frame_to_send.data) {
            sender_.send(frame_to_send.data, frame_to_send.length);
            
            // 送信し終わったらメモリを解放
            delete[] static_cast<uint8_t*>(frame_to_send.data);
        }
    }
}
