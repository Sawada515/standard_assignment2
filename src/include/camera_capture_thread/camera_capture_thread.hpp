/**
 * @file    camera_capture_thread.hpp
 * @author  sawada souta
 * @brief   Webカメラから画像データを取得するスレッド 
 * @version 0.1
 * @date    2025-12-14
 */

#ifndef CAMERA_CAPTURE_THREAD_HPP_
#define CAMERA_CAPTURE_THREAD_HPP_

#include <cstdint>
#include <string>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>

#include "camera/v4l2_capture.hpp"

/**
 * @brief   Webカメラから画像データを取得するスレッドクラス	
 * @note    mutexとcondition_variableでスレッドセーフに実装
 */
class CameraCaptureThread {
public:
    /**
     * @brief       コンストラクタ
     * @param[in]   width 取得する画像データの横幅
     * @param[in]   height 取得する画像データの縦幅
     * @param[in]   device /dev/video[0-9]のデバイスパス
     */
    CameraCaptureThread(uint32_t width, uint32_t height, std::string& device);

    /**
     * @brief       デコンストラクタ
     */
    ~CameraCaptureThread();

    /**
     * @brief       キャプチャスレッド開始
     */
    void start(void);

    /**
     * @brief       キャプチャスレッド停止
     */
    void stop(void);

    /**
     * @brief       キャプチャしたフレームデータを取得
     * @param[in]   frame フレーム情報を渡す
     * @return      true エラーなし
     * @return      false エラーあり
     * @note        エラーの場合はログを参照
     */
    bool getframe(V4L2Capture::Frame& frame);

private:
    void captureLoop(void);

    std::thread capture_thread_;
    std::mutex mutex_;
    std::condition_variable cond_var_;
    std::queue<V4L2Capture::Frame> frame_queue_;

    bool running_;

    std::string device_name_;

    V4L2Capture camera_;
};

#endif
