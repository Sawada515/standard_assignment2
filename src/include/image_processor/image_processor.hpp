/**
 * @file    image_processor.hpp
 * @brief   画像処理クラス (GUI用加工 & AI解析)
 * @author  sawada souta
 * @date    2025-12-14
 */

#ifndef IMAGE_PROCESSOR_HPP_
#define IMAGE_PROCESSOR_HPP_

#include <vector>
#include <cstdint>

#include <turbojpeg.h>
#include <opencv2/opencv.hpp>

/**
 * @brief YUYV入力を前提とした画像処理クラス
 */
class ImageProcessor {
public:
    /**
     * @brief GUI送信用の処理結果
     */
    struct GuiProcessedData {
        std::vector<uint8_t> encoded_jpeg;  /**< GUI送信用JPEG */
        cv::Mat bgr_mat;                    /**< AI/デバッグ用BGR画像 */
    };

    /**
     * @brief AI解析用の処理結果
     */
    struct AiProcessedData {
        cv::Mat gray_mat;
        cv::Mat binary_mat;
    };

    /**
     * @brief       コンストラクタ
     * @param[in]   quality JPEGエンコード品質 (1~100)
     * @param[in]   resize_width リサイズ後の幅 (pixel)
     */
    ImageProcessor(uint8_t quality, double resize_width);
    ~ImageProcessor();

    /**
     * @brief       GUI送信用に画像を加工する
     * @param[in]   yuyv       YUYV生データ
     * @param[in]   width      画像幅
     * @param[in]   height     画像高さ
     * @param[out]  output     加工後データ
     * @return      true 成功
     */
    bool process_for_gui(const uint8_t* yuyv,
                         uint32_t width,
                         uint32_t height,
                         GuiProcessedData& output);

    /**
     * @brief       AI解析用に画像を加工する
     * @param[in]   bgr_img    process_for_guiで生成されたBGR画像
     * @param[out]  output     解析用データ
     * @return      true 成功
     */
    bool process_for_ai(const cv::Mat& bgr_img,
                        AiProcessedData& output);

    /**
     * @brief コントラスト・明るさ設定
     */
    void setContrast(double alpha, double beta);

private:
    /**
     * @brief YUYV → BGR 変換
     */
    bool yuyv_to_bgr(const uint8_t* yuyv,
                     uint32_t width,
                     uint32_t height,
                     cv::Mat& bgr);

    /**
     * @brief BGR → JPEG エンコード
     */
    bool encode_to_jpeg(const cv::Mat& bgr,
                        std::vector<uint8_t>& encoded);

private:
    uint8_t quality_;

    double contrast_alpha_;
    double brightness_beta_;
    double resize_width_;

    tjhandle tj_enc_;

    std::vector<uint8_t> jpeg_buffer_;
};

#endif
