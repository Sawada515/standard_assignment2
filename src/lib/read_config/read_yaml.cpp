/**
 * @file        read_yaml.hpp
 * @brief       YAML設定ファイル読み込みクラス
 * @author      sawada souta
 * @date        2025-12-15
 */

#include <yaml-cpp/yaml.h>
#include <iostream>

#include "read_config/read_yaml.hpp"
#include "logger/logger.hpp"

// コンストラクタ
ReadYaml::ReadYaml()
{
    config_data_.network.dest_ip = "127.0.0.1";
    config_data_.network.top_view_port = 50000;
    config_data_.network.bottom_view_port = 50001;

    config_data_.camera.top_view_device = "/dev/video0";
    config_data_.camera.bottom_view_device = "/dev/video2";
    config_data_.camera.width = 800;
    config_data_.camera.height = 600;

    config_data_.image_processor.jpeg_quality = 80;
    config_data_.image_processor.resize_width = 640.0;
}

// デストラクタ
ReadYaml::~ReadYaml()
{
}

// 設定ファイルを読み込む
bool ReadYaml::load_config(const std::string& filepath)
{
    try {
        YAML::Node config = YAML::LoadFile(filepath);

        if(config["network"]) {
            auto net = config["network"];

            config_data_.network.dest_ip = net["dest_ip"].as<std::string>();
            config_data_.network.top_view_port = net["top_view_port"].as<uint16_t>();
            config_data_.network.bottom_view_port = net["bottom_view_port"].as<uint16_t>();
        }

        if(config["camera"]) {
            auto cam = config["camera"];

            config_data_.camera.top_view_device = cam["top_view_device"].as<std::string>();
            config_data_.camera.bottom_view_device = cam["bottom_view_device"].as<std::string>();
            config_data_.camera.width = cam["width"].as<uint32_t>();
            config_data_.camera.height = cam["height"].as<uint32_t>();
        }

        if(config["image_processor"]) {
            auto img_proc = config["image_processor"];

            auto tmp = img_proc["jpeg_quality"].as<int>();
            config_data_.image_processor.jpeg_quality = static_cast<uint8_t>(tmp);

            config_data_.image_processor.resize_width = img_proc["resize_width"].as<double>();
        }
    } catch (const YAML::BadFile& e) {
        LOG_E("Failed to open config file: %s", e.what());

        return false;
    } catch (const YAML::Exception& e) {
        LOG_E("YAML parsing error: %s", e.what());

        return false;
    } catch (const std::exception& e) {
        LOG_E("Error loading config file: %s", e.what());

        return false;
    }

    return true;
}

// 読み込んだ設定データを取得する
const AppConfigData& ReadYaml::get_config_data() const
{
    return config_data_;
}
