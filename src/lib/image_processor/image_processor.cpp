/**
 * @file    image_processor.cpp
 * @brief   画像処理クラスの実装
 * @author  sawada souta
 * @date    2025-12-14
 */

#include <vector>

#include "image_processor/image_processor.hpp"
#include "logger/logger.hpp"

// コンストラクタ
ImageProcessor::ImageProcessor()
    : contrast_alpha_(1.0),
      brightness_beta_(0.0)
{
}

// デストラクタ
ImageProcessor::~ImageProcessor()
{
}

// コントラストと明るさの設定
void ImageProcessor::setContrast(double alpha, double beta)
{
    contrast_alpha_ = alpha;
    brightness_beta_ = beta;
}

// GUI用（標準）処理パイプライン
bool ImageProcessor::process_for_gui(const V4L2Capture::Frame& frame, StdProcessedData& output_data)
{
    if (frame.data == nullptr || frame.length == 0) {
        LOG_E("Invalid input frame data");

        return false;
    }

    // 1. V4L2の生データ(MJPEG等)をOpenCVのMat(BGR)にデコード
    // データポインタから1次元のMatを作成 (コピーせず参照のみ)
    cv::Mat raw_wrapper(1, frame.length, CV_8UC1, frame.data);
    
    // 画像としてデコード
    cv::Mat img = cv::imdecode(raw_wrapper, cv::IMREAD_COLOR);

    if (img.empty()) {
        LOG_E("Failed to decode image from V4L2 frame");

        return false;
    }

    // 2. ノイズ除去 (ガウシアンフィルタ)
    // カーネルサイズ(5,5), 標準偏差0(自動計算)
    cv::GaussianBlur(img, img, cv::Size(5, 5), 0);

    // 3. コントラスト・明度補正
    // 計算式: New = alpha * Old + beta
    // alpha 1.0 = そのまま, >1.0 = コントラスト強調
    // beta  0 = そのまま, >0 = 明るく
    img.convertTo(img, -1, contrast_alpha_, brightness_beta_);

    // 4. 結果の格納 (解析用として生データを保持)
    output_data.raw_mat = img;

    // 5. 送信用にJPEGエンコード (GUI表示用)
    // GUI側できれいな画像を見るため、ここでの画質は標準的(80)に設定
    if (!encode_image_to_jpeg(img, output_data.send_encoded_image, 80)) {
        LOG_E("Failed to encode standard image");

        return false;
    }

    return true;
}

// AI解析用処理パイプライン
bool ImageProcessor::process_for_ai(const cv::Mat& src_img, AnalysisProcessedData& output_data)
{
    if (src_img.empty()) {
        LOG_E("Input image for AI processing is empty");

        return false;
    }

    // 解析用にグレースケール変換
    cv::Mat gray, binary;
    cv::cvtColor(src_img, gray, cv::COLOR_BGR2GRAY);

    // 2値化 (大津の二値化: 閾値を自動決定)
    // 基板上の配線や部品を背景から分離するのに有効
    cv::threshold(gray, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

    // 輪郭抽出
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    // RETR_EXTERNAL: 最外周の輪郭のみ抽出 (部品の外形など)
    // CHAIN_APPROX_SIMPLE: 点を間引いてメモリ節約
    cv::findContours(binary, contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    // 結果画像の作成
    // 元画像をコピーして、その上に検出結果（輪郭）を描画する
    output_data.std_processed_mat = src_img.clone();

    // 輪郭の描画 (全ての輪郭を赤色で描画)
    // 色: B=0, G=0, R=255, 太さ: 2px
    cv::drawContours(output_data.std_processed_mat, contours, -1, cv::Scalar(0, 0, 255), 2);

    // AI解析結果の可視化画像をJPEGエンコードして送信データにする
    // 通信帯域節約のため、解析結果確認用なら画質を少し落としても良い(例: 70)
    if (!encode_image_to_jpeg(output_data.std_processed_mat, output_data.send_encoder_image, 70)) {
        LOG_E("Failed to encode analysis image");

        return false;
    }

    return true;
}

// 共通処理: JPEGエンコード
bool ImageProcessor::encode_image_to_jpeg(const cv::Mat& img, std::vector<uint8_t>& encoded_buffer, uint8_t quality)
{
    if (img.empty()) {
        return false;
    }

    std::vector<int> params = {
        cv::IMWRITE_JPEG_QUALITY, static_cast<int>(quality)
    };

    try {
        // .jpg形式でメモリバッファに出力
        cv::imencode(".jpg", img, encoded_buffer, params);
    } catch (const cv::Exception& ex) {
        LOG_E("OpenCV exception in encode_image_to_jpeg: %s", ex.what());
        
        return false;
    }

    return !encoded_buffer.empty();
}
