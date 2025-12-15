#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>

#include "camera/camera_capture_thread.hpp"
#include "network/udp_sender_thread.hpp"
#include "image_processor/image_processor.hpp" 
#include "logger/logger.hpp"
#include "read_config/read_yaml.hpp"

volatile std::sig_atomic_t g_signal_status = 0;

void signal_handler(int signal) {
    g_signal_status = signal;
}

int main() {
    ReadYaml config_reader;

    if (!config_reader.load_config("../config/config.yaml")) {
        LOG_E("Failed to load configuration file.");

        return -1;
    }

    AppConfigData config_data;
    config_data = config_reader.get_config_data();

    std::signal(SIGINT, signal_handler);
    LOG_I("System Starting...");

    CameraCaptureThread cam_top_view( \
        config_data.camera.width, \
        config_data.camera.height, \
        config_data.camera.top_view_device);
    CameraCaptureThread cam_bottom_view( \
        config_data.camera.width, \
        config_data.camera.height, \
        config_data.camera.bottom_view_device);
    
    UDPSenderThread top_view_sender(
        config_data.network.dest_ip, \
        config_data.network.top_view_port); 
    UDPSenderThread bottom_view_sender(
        config_data.network.dest_ip, \
        config_data.network.bottom_view_port);

    ImageProcessor processor(config_data.image_processor.jpeg_quality, \
        config_data.image_processor.resize_width);

    top_view_sender.start();
    cam_top_view.start();

    bottom_view_sender.start();
    cam_bottom_view.start();

    LOG_I("Streaming Started to %s:%d", \
        config_data.network.dest_ip.c_str(), \
        config_data.network.top_view_port);
    LOG_I("Streaming Started to %s:%d", \
        config_data.network.dest_ip.c_str(), \
        config_data.network.bottom_view_port);

    while (g_signal_status == 0) {
        V4L2Capture::Frame top_view_frame;
        V4L2Capture::Frame bottom_view_frame;

        if (cam_top_view.getframe(top_view_frame)) {
            
            ImageProcessor::StdProcessedData processed;
            
            if (processor.process_for_gui(top_view_frame, processed)) {
                
                if (!processed.send_encoded_image.empty()) {
                    top_view_sender.enqueue_vector(processed.send_encoded_image);
                }
            }
            
            if (top_view_frame.data) {
                delete[] static_cast<uint8_t*>(top_view_frame.data);
            }

        }
        if (cam_bottom_view.getframe(bottom_view_frame)) {
            ImageProcessor::StdProcessedData processed;

            if (processor.process_for_gui(bottom_view_frame, processed)) {

                if (!processed.send_encoded_image.empty()) {
                    bottom_view_sender.enqueue_vector(processed.send_encoded_image);
                }
            }

            if (bottom_view_frame.data) {
                delete[] static_cast<uint8_t*>(bottom_view_frame.data);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    cam_top_view.stop();
    top_view_sender.stop();

    cam_bottom_view.stop();
    bottom_view_sender.stop();

    LOG_I("System Stopped.");

    return 0;
}
