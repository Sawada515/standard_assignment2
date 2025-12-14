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

#include <opencv2/opencv.hpp>

#include "camera/v4l2_capture.hpp"

class ImageProcessor {
public:
    struct StdProcessedData {
        std::vector<uint8_t> send_encoded_image;
        cv::Mat raw_mat;
    };

    struct AnalysisProcessedData {
        std::vector<uint8_t> send_encoder_image;
        cv::Mat std_processed_mat;
    };

    ImageProcessor();
    ~ImageProcessor();

    /**
     * @brief       GUI送信用に画像を加工する（パイプライン前半）
     * @param[in]   frame カメラからの生データ
     * @param[out]  output_data 加工・圧縮後のデータ
     * @return      true 成功
     * @return      false 失敗
     * @note        エラーの場合、output_dataの内容は不定
     * @details     ノイズ除去 -> コントラスト補正
     */
    bool process_for_gui(const V4L2Capture::Frame& frame, StdProcessedData& output_data);

    /**
     * @brief       AI解析用に画像を加工する（パイプライン後半）
     * @param[in]   src_img processForGuiで生成されたMat画像
     * @param[out]  output_data 加工後のデータ
     * @return      true 成功
     * @return      false 失敗
     * @note        エラーの場合、内部状態は不定
     * @details     グレースケール -> 2値化 -> エッジ/輪郭 
     */
    bool process_for_ai(const cv::Mat& src_img, AnalysisProcessedData& output_data);

    /**
     * @brief       画像をJPEGエンコードする
     * @param[in]   img エンコードする画像データ
     * @param[out]  encoded_buffer エンコード後のバッファ
     * @param[in]   quality JPEGエンコード品質 (1~100)
     * @return      true 成功
     * @return      false 失敗
     * @note        エラーの場合、encoded_bufferの内容は不定
     */
    bool encode_image_to_jpeg(const cv::Mat& img, std::vector<uint8_t>& encoded_buffer, uint8_t quality);

    /**
     * @brief Set the Contrast object
     * @param[in] alpha 
     * @param[in] beta 
     */
    void setContrast(double alpha, double beta); // alpha:コントラスト(1.0~3.0), beta:明るさ(0~100)

private:
    double contrast_alpha_;
    double brightness_beta_;
};

#endif