/**
 * @file    udp_sender_thread.cpp
 * @brief   画像データ送信専用のスレッドクラス実装
 * @author  sawada souta
 * @date    2025-12-14
 */

#include <cstring>

#include "network/udp_sender_thread.hpp"
#include "camera/v4l2_capture.hpp"
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
    send_thread_ = std::thread(&UDPSenderThread::send_loop, this);
    
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

    std::lock_guard<std::mutex> lock(mutex_);
    while (!send_queue_.empty()) {
        send_queue_.pop();
    }

    LOG_I("UDP sender thread stopped");
}

void UDPSenderThread::enqueue(V4L2Capture::Frame&& frame)
{
    if (!running_) return;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (send_queue_.size() > 10) {
            send_queue_.pop();
        }

        send_queue_.push(std::move(frame));
    }

    cond_var_.notify_one();
}

void UDPSenderThread::send_loop(void)
{
    while (running_) {
        V4L2Capture::Frame frame;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            
            cond_var_.wait(lock, [this]() {
                return !send_queue_.empty() || !running_;
            });

            if (!running_) {
                break;
            }

            frame = std::move(send_queue_.front());

            send_queue_.pop();
        }

        if (frame.data) {
            sender_.send(frame.data, frame.length);
        }
    }
}
