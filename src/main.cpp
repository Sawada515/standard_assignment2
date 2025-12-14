#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <vector>

#include "camera/camera_capture_thread.hpp"
#include "logger/logger.hpp"

// Ctrl+C で終了するためのフラグ
volatile std::sig_atomic_t g_signal_status = 0;

void signal_handler(int signal) {
    g_signal_status = signal;
}

int main() {
    // シグナルハンドラの設定 (Ctrl+C用)
    std::signal(SIGINT, signal_handler);

    // Loggerの設定（もし必要なら初期化処理など）
    // Logger::init(); 

    std::cout << "Starting Camera Application..." << std::endl;

    // 1. カメラ・スレッドのインスタンス化
    // 幅: 640, 高さ: 480, デバイス: /dev/video0
    CameraCaptureThread cam_thread(640, 480, "/dev/video0");

    // 2. キャプチャ開始
    cam_thread.start();

    int frame_count = 0;

    // 3. メインループ
    while (g_signal_status == 0) {
        V4L2Capture::Frame frame;

        // フレーム取得を試みる
        if (cam_thread.getframe(frame)) {
            frame_count++;
            
            // ログ出力 (例: 30フレームごとに表示)
            if (frame_count % 30 == 0) {
                std::cout << "[Main] Captured Frame #" << frame_count 
                          << " | Size: " << frame.length << " bytes" 
                          << " | Address: " << frame.data << std::endl;
            }

            // 【重要】メモリ解放
            // CameraCaptureThread内で 'new' されたデータなので
            // 必ずここで delete[] する必要があります。
            if (frame.data) {
                delete[] static_cast<uint8_t*>(frame.data);
            }

        } else {
            // フレームが来ていないときは少し待機してCPUを休める
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    std::cout << "\nStopping Application..." << std::endl;

    // 4. 停止処理
    cam_thread.stop();

    std::cout << "Application check finished." << std::endl;

    return 0;
}
