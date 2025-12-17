#include <csignal>
#include <thread>
#include <chrono>

#include "logger/logger.hpp"
#include "read_config/read_yaml.hpp"
#include "camera/v4l2_capture.hpp"
#include "network/udp_sender_thread.hpp"
#include "image_processor/image_processor.hpp"

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

    V4L2Capture top_view_cam(
        config.camera.top_view_device,
        config.camera.width,
        config.camera.height);

    V4L2Capture bottom_view_cam(
        config.camera.bottom_view_device,
        config.camera.width,
        config.camera.height);


    UDPSenderThread top_view_sender(
        config.network.dest_ip,
        config.network.top_view_port);

    UDPSenderThread bottom_view_sender(
        config.network.dest_ip,
        config.network.bottom_view_port);

    top_view_sender.start();
    bottom_view_sender.start();

    ImageProcessor processor(
        config.image_processor.jpeg_quality,
        config.image_processor.resize_width);

    LOG_I("Streaming Loop Start");

    while (g_signal_status == 0) {
        auto loop_start = std::chrono::steady_clock::now();

        {
            V4L2Capture::Frame frame;
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
                }
            }
        }

        {
            V4L2Capture::Frame frame;
            if (bottom_view_cam.get_once_frame(frame)) {

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
                        bottom_view_sender.enqueue(
                            std::move(gui.image));
                    }
                }
            }
        }

        std::this_thread::sleep_until(loop_start + std::chrono::milliseconds(250));
    }

    top_view_sender.stop();
    bottom_view_sender.stop();

    LOG_I("Debug GUI Streaming Stop");

    return 0;
}
