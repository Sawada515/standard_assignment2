/**
 * @file    udp_sender_thread.hpp
 * @brief   画像データ送信専用のスレッドクラス
 * @author  sawada souta
 * @date    2025-12-14
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

#include "camera/v4l2_capture.hpp"
#include "network/udp_sender.hpp"

/**
 * @brief 画像データを非同期（別スレッド）でUDP送信するクラス
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
     * @brief 送信キューに画像データを追加する
     * @param[in] frame 送信する画像フレーム
     * @note 内部でデータのディープコピーを行います
     */
    void enqueue(const V4L2Capture::Frame& frame);

    /**
     * @brief       送信キューに画像データを追加する
     * @param[in]   data 送信する画像データのバイト列
     * @note        内部でデータのディープコピーを行います
     */
    void enqueue_vector(const std::vector<uint8_t>& data);

private:
    /**
     * @brief 送信ループ（スレッド関数）
     */
    void sendLoop(void);

    UDPSender sender_;

    std::thread send_thread_;
    std::mutex mutex_;
    std::condition_variable cond_var_;
    
    std::queue<V4L2Capture::Frame> send_queue_;

    bool running_;
};

#endif
