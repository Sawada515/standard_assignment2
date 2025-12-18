#include <csignal>
#include <thread>
#include <chrono>

#include "logger/logger.hpp"
#include "read_config/read_yaml.hpp"
#include "camera/v4l2_capture.hpp"
#include "network/udp_sender_thread.hpp"
#include "image_processor/image_processor.hpp"

#include <opencv2/opencv.hpp>

#define MODEL_PATH "../train_data/best.onnx"

volatile std::sig_atomic_t g_signal_status = 0;

void signal_handler(int signal)
{
    g_signal_status = signal;
}

int main()
{
    /* ---------- 設定読み込み ---------- */
    ReadYaml config_reader;
    if (!config_reader.load_config("../config/config.yaml")) {
        LOG_E("Failed to load configuration file.");
        return -1;
    }

    const AppConfigData config = config_reader.get_config_data();

    std::signal(SIGINT, signal_handler);
    LOG_I("Debug GUI Streaming Start");

    /* ---------- カメラインスタンス生成 ---------- */
    V4L2Capture top_view_cam(
        config.camera.top_view_device,
        config.camera.width,
        config.camera.height);

    LOG_I("Initializing Top View Camera...");
    if (!top_view_cam.initialize()) {
        LOG_E("Failed to initialize Top View Camera (%s)", config.camera.top_view_device.c_str());
        return -1;
    }

    /* ---------- ネットワーク送信設定 ---------- */
    UDPSenderThread top_view_sender(
        config.network.dest_ip,
        config.network.top_view_port);

    top_view_sender.start();

    /* ---------- 画像処理設定 ---------- */
    ImageProcessor processor(
        MODEL_PATH,
        config.image_processor.jpeg_quality,
        config.image_processor.resize_width);

    LOG_I("Streaming Loop Start");

    while (g_signal_status == 0) {
        auto loop_start = std::chrono::steady_clock::now();

        // -------------------------------------------------
        // Top Camera Process
        // -------------------------------------------------
        {
            V4L2Capture::Frame frame;
            // 内部で STREAMON -> 撮影 -> STREAMOFF を行う
            // (デバイスはOpenされたままなので高速)
            if (top_view_cam.get_once_frame(frame)) {

                ImageProcessor::GuiProcessedData gui;
                ImageProcessor::AiProcessedData  ai;

                if (processor.process_frame(
                        frame.data.data(),
                        frame.width,
                        frame.height,
                        gui,
                        ai))
                {
                    if (gui.is_jpeg && !gui.image.empty()) {
                        top_view_sender.enqueue(
                            std::move(gui.image));
                    }

                    if (!ai.image.empty()) {
                        cv::Mat top_mat(ai.height, ai.width, CV_8UC3, ai.image.data());

                        cv::imwrite("/home/shikoku-pc/img/tmp_top.jpeg", top_mat);
                    } else {
                        LOG_W("top ai image is empty");
                    }
                }
            } else {
                LOG_W("Failed to capture frame from Top Camera");
            }
        }

        std::this_thread::sleep_until(loop_start + std::chrono::milliseconds(250));
    }

    top_view_sender.stop();

    LOG_I("Debug GUI Streaming Stop");

    return 0;
}
