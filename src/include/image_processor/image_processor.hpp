/**
 * @file    image_processor.hpp
 * @brief   画像処理（YUYV入力 → GUI / AI 出力）
 * @author  sawada souta
 * @date    2025-12-16
 */

#ifndef IMAGE_PROCESSOR_HPP_
#define IMAGE_PROCESSOR_HPP_

#include <cstdint>
#include <vector>

/**
 * @brief 画像処理クラス
 * @note  カメラ・ネットワーク・GUIとは独立
 */
class ImageProcessor {
public:
    /**
     * @brief GUI表示用の処理済みデータ
     */
    struct GuiProcessedData {
        std::vector<uint8_t> image; /**< BGR or JPEG */
        uint32_t width  = 0;
        uint32_t height = 0;
        bool is_jpeg = false;
    };

    /**
     * @brief AI（YOLO）入力用データ
     */
    struct AiProcessedData {
        std::vector<uint8_t> image; /**< BGR or RGB */
        uint32_t width  = 0;
        uint32_t height = 0;
        uint32_t channels = 3;      /**< usually 3 */
    };

    /**
     * @brief コンストラクタ
     */
    ImageProcessor(int jpeg_quality, uint32_t resize_width);

    /**
     * @brief デストラクタ
     */
    ~ImageProcessor();

    /**
     * @brief YUYVフレームを入力して処理する
     * @param[in]  yuyv       YUYVフォーマットの画像
     * @param[in]  width      画像幅
     * @param[in]  height     画像高さ
     * @param[out] gui_data   GUI表示用データ
     * @param[out] ai_data    AI入力用データ
     * @return true 成功
     */
    bool process_frame(const uint8_t* yuyv,
                       uint32_t width,
                       uint32_t height,
                       GuiProcessedData& gui_data,
                       AiProcessedData& ai_data);
    
    /**
    * @brief YUYV画像をJPEGに変換（デバッグ用）
    * @param[in]  yuyv     YUYV画像データ
    * @param[in]  width    画像幅
    * @param[in]  height   画像高さ
    * @param[in]  quality  JPEG品質 (1-100)
    * @param[out] jpeg     JPEGデータ（所有権あり）
    * @return true 成功
    */
bool yuyv_to_jpeg_debug(const uint8_t* yuyv,
                        uint32_t width,
                        uint32_t height,
                        int quality,
                        std::vector<uint8_t>& jpeg);

private:
    /**
     * @brief YUYV → BGR 変換（全体）
     */
    void yuyv_to_bgr(const uint8_t* yuyv,
                     uint32_t width,
                     uint32_t height,
                     std::vector<uint8_t>& bgr);

    /**
     * @brief 抵抗候補抽出（Y成分のみ使用）
     * @note 高速化のため整数演算のみ
     */
    void extract_resistor_candidates(const uint8_t* yuyv,
                                     uint32_t width,
                                     uint32_t height,
                                     std::vector<uint8_t>& binary);

    /**
     * @brief GUI用にリサイズ（必要な場合）
     */
    void resize_for_gui(const std::vector<uint8_t>& src,
                        uint32_t src_w,
                        uint32_t src_h,
                        uint32_t dst_w,
                        uint32_t dst_h,
                        std::vector<uint8_t>& dst);

    int jpeg_quality_;          /**< JPEG品質 */
    uint32_t resize_width_;     /**< GUI用リサイズ幅 */
};

#endif /* IMAGE_PROCESSOR_HPP_ */
