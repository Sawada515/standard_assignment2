/**
 * @file    udp_sender_thread.hpp
 * @brief   バイト列データを非同期でUDP送信するスレッドクラス
 * @author  sawada souta
 * @version 0.2
 * @date    2025-12-16
 */

#ifndef UDP_SENDER_THREAD_HPP_
#define UDP_SENDER_THREAD_HPP_

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <string>
#include <vector>
#include <cstdint>

#include "network/udp_sender.hpp"

/**
 * @brief 完成済みデータを非同期（別スレッド）でUDP送信するクラス
 */
class UDPSenderThread {
public:
    /**
     * @brief コンストラクタ
     * @param[in] ip   送信先IPアドレス
     * @param[in] port 送信先ポート番号
     */
    UDPSenderThread(const std::string& ip, uint16_t port);

    ~UDPSenderThread();

    /**
     * @brief 送信スレッドを開始する
     */
    void start(void);

    /**
     * @brief 送信スレッドを停止する
     */
    void stop(void);

    /**
     * @brief 送信キューにデータを追加する
     * @param[in] data 送信するバイト列（所有権は内部へムーブ）
     */
    void enqueue(std::vector<uint8_t>&& data);

private:
    /**
     * @brief 送信ループ（スレッド関数）
     */
    void send_loop(void);

    UDPSender sender_;

    std::thread send_thread_;
    std::mutex mutex_;
    std::condition_variable cond_var_;

    std::queue<std::vector<uint8_t>> send_queue_;

    bool running_;
};

#endif
