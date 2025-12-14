#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>

#include "camera/camera_capture_thread.hpp"
#include "network/udp_sender_thread.hpp"
#include "image_processor/image_processor.hpp" // ディレクトリ名修正済み
#include "logger/logger.hpp"

// Ctrl+C で終了するためのフラグ
volatile std::sig_atomic_t g_signal_status = 0;

void signal_handler(int signal) {
    g_signal_status = signal;
}

int main() {
    // シグナルハンドラの設定
    std::signal(SIGINT, signal_handler);
    LOG_I("System Starting...");

    // 1. 各モジュールの初期化

    // カメラ設定: /dev/video0, 640x480
    CameraCaptureThread cam_thread(800, 600, "/dev/video2");
    
    // 【ここを変更しました】
    // 送信先IP: 192.168.10.200, ポート: 50000
    UDPSenderThread sender_thread("192.168.10.200", 50000); 

    // 画像処理クラス
    ImageProcessor processor;
    processor.setContrast(1.0, 0.0); // コントラスト設定(必要に応じて変更)

    // 2. スレッド開始
    sender_thread.start();
    cam_thread.start();

    LOG_I("Streaming Started to 192.168.10.200:50000");

    // 3. メインループ
    while (g_signal_status == 0) {
        V4L2Capture::Frame frame;

        // カメラからフレーム取得
        if (cam_thread.getframe(frame)) {
            
            // --- 画像処理 & 圧縮 ---
            ImageProcessor::StdProcessedData processed;
            
            // GUI送信用に処理 (ノイズ除去 -> コントラスト -> JPEG圧縮)
            if (processor.process_for_gui(frame, processed)) {
                
                // --- 送信 ---
                // 圧縮されたJPEGデータを送信キューに入れる
                if (!processed.send_encoded_image.empty()) {
                    sender_thread.enqueue_vector(processed.send_encoded_image);
                }
            }
            
            // --- (必要なら) AI解析処理などをここに記述 ---
            // ImageProcessor::AnalysisProcessedData analysis_data;
            // processor.process_for_ai(processed.raw_mat, analysis_data);

            // メモリ解放 (カメラから貰った生データはもう不要)
            if (frame.data) {
                delete[] static_cast<uint8_t*>(frame.data);
            }

        } else {
            // フレームが来ていないときは少し休む (CPU負荷低減)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // 終了処理
    cam_thread.stop();
    sender_thread.stop();
    LOG_I("System Stopped.");

    return 0;
}
