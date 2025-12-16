#include <iostream>
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
    ReadYaml config_reader;

    if (!config_reader.load_config("../config/config.yaml")) {
        LOG_E("Failed to load configuration file.");
        return -1;
    }

    AppConfigData config_data = config_reader.get_config_data();

    std::signal(SIGINT, signal_handler);
    LOG_I("System Starting...");

    V4L2Capture top_view_cam(
        config_data.camera.top_view_device,
        config_data.camera.width,
        config_data.camera.height);

    V4L2Capture bottom_view_cam(
        config_data.camera.bottom_view_device,
        config_data.camera.width,
        config_data.camera.height);

    UDPSenderThread top_view_sender(
        config_data.network.dest_ip,
        config_data.network.top_view_port);

    UDPSenderThread bottom_view_sender(
        config_data.network.dest_ip,
        config_data.network.bottom_view_port);

    ImageProcessor processor(
        config_data.image_processor.jpeg_quality,
        config_data.image_processor.resize_width);

    if (!top_view_cam.open_device()) {
        LOG_E("Failed to open top view camera device.");

        return -1;
    }

    if (!bottom_view_cam.open_device()) {
        LOG_E("Failed to open bottom view camera device.");

        return -1;
    }

    top_view_sender.start();
    bottom_view_sender.start();

    LOG_I("Streaming Started");

    while (g_signal_status == 0) {
        auto loop_start = std::chrono::steady_clock::now();

        {
            V4L2Capture::Frame frame;

            if (top_view_cam.get_frame(frame)) {
                ImageProcessor::GuiProcessedData processed;

                if (processor.process_for_gui(frame.data, frame.width, frame.height, processed)) {
                    if (!processed.encoded_jpeg.empty()) {
                        top_view_sender.enqueue(
                            std::move(processed.encoded_jpeg));
                    }
                }
            }
        }

        {
            V4L2Capture::Frame frame;

            if (bottom_view_cam.get_frame(frame)) {
                ImageProcessor::GuiProcessedData processed;

                if (processor.process_for_gui(frame.data, frame.width, frame.height, processed)) {
                    if (!processed.encoded_jpeg.empty()) {
                        bottom_view_sender.enqueue(
                            std::move(processed.encoded_jpeg));
                    }
                }
            }
        }

        /* 1秒周期 */
        std::this_thread::sleep_until(
            loop_start + std::chrono::seconds(1));
    }

    top_view_sender.stop();
    bottom_view_sender.stop();

    top_view_cam.close_device();
    bottom_view_cam.close_device();

    LOG_I("System Stopped.");

    return 0;
}
