/**
 * @file    image_processor.hpp
 * @brief   画像処理（YUYV入力 → GUI / AI 出力）- 負荷軽減版
 * @author  sawada souta
 * @date    2025-12-17
 */

#ifndef IMAGE_PROCESSOR_HPP_
#define IMAGE_PROCESSOR_HPP_

#include <cstdint>
#include <vector>
#include <opencv2/core.hpp>


/**
 * @brief 画像処理クラス
 * @note  カメラ・ネットワーク・GUIとは独立
 */


class ImageProcessor {
public:
    struct GuiProcessedData {
        std::vector<uint8_t> image;
        uint32_t width  = 0;
        uint32_t height = 0;
        bool is_jpeg = false;
    };

    struct AiProcessedData {
        std::vector<uint8_t> image; // BGR Data
        uint32_t width  = 0;
        uint32_t height = 0;
        uint32_t channels = 3;
    };

    ImageProcessor(int jpeg_quality, uint32_t resize_width);
    ~ImageProcessor();

    bool process_frame(const uint8_t* yuyv,
                       uint32_t width,
                       uint32_t height,
                       GuiProcessedData& gui_data,
                       AiProcessedData& ai_data);

private:
    /**
     * @brief BGR画像をJPEGに圧縮 (TurboJPEG使用)
     */
    bool bgr_to_jpeg(const std::vector<uint8_t>& bgr,
                     uint32_t width,
                     uint32_t height,
                     int quality,
                     std::vector<uint8_t>& jpeg);

    int jpeg_quality_;
    uint32_t resize_width_;
};

#endif
