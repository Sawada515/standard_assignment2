/**
 * @file    udp_sender_thread.cpp
 * @brief   バイト列データを非同期でUDP送信するスレッドクラス実装
 * @author  sawada souta
 * @version 0.2
 * @date    2025-12-16
 */

#include "network/udp_sender_thread.hpp"
#include "logger/logger.hpp"

#define MAX_QUEUE_SiZE 1  /**< 送信キューの最大サイズ */

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

    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }

    cond_var_.notify_all();

    if (send_thread_.joinable()) {
        send_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!send_queue_.empty()) {
            send_queue_.pop();
        }
    }

    LOG_I("UDP sender thread stopped");
}

void UDPSenderThread::enqueue(std::vector<uint8_t>&& data)
{
    if (!running_) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 常に最新のフレームを送るために捨てる
        while (!send_queue_.empty()) {
            send_queue_.pop();
        }

        send_queue_.push(std::move(data));
    }

    cond_var_.notify_one();
}

void UDPSenderThread::send_loop(void)
{
    while (true) {
        std::vector<uint8_t> packet;

        {
            std::unique_lock<std::mutex> lock(mutex_);

            cond_var_.wait(lock, [this]() {
                return !send_queue_.empty() || !running_;
            });

            if (!running_ && send_queue_.empty()) {
                break;
            }

            packet = std::move(send_queue_.front());
            send_queue_.pop();
        }

        if (!packet.empty()) {
            sender_.send(packet.data(), packet.size());
        }
    }
}
