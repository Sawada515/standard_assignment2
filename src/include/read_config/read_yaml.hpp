/**
 * @file        read_yaml.hpp
 * @brief       YAML設定ファイル読み込みクラス
 * @author      sawada souta
 * @date        2025-12-15
 */

#ifndef READ_YAML_HPP_
#define READ_YAML_HPP_

#include <string>
#include <cstdint>

struct AppConfigData {
    struct Network {
        std::string dest_ip;
        uint16_t top_view_port;
        uint16_t bottom_view_port;
    } network;

    struct Camera {
        std::string top_view_device;
        std::string bottom_view_device;
        uint32_t width;
        uint32_t height;
    } camera;

    struct ImageProcessor {
        uint8_t jpeg_quality;
        double resize_width;
    } image_processor;
};

/**
 * @brief   YAML設定ファイル読み込みクラス
 */
class ReadYaml {
    public:
	    /**
	     * @brief       コンストラクタ
	     */
	    ReadYaml();
	    /**
	     * @brief       デストラクタ
	     */
	    ~ReadYaml();
	
        /**
         * @brief       設定ファイルを読み込む
         * @param[in]   filepath 設定ファイルパス
         * @return      true 読み込み成功
         * @return      false 読み込み失敗
         * @note        読み込み失敗時、内部状態は不定
         */
	    bool load_config(const std::string& filepath);
	
        /**
         * @brief       読み込んだ設定データを取得する
         * @return      設定データ構造体へのconst参照
         */
	    const AppConfigData& get_config_data() const;

    private:
        AppConfigData config_data_;
};

#endif