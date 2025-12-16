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
ImageProcessor::ImageProcessor(uint8_t quality, double resize_width)
    : quality_(quality),
      contrast_alpha_(1.0),
      brightness_beta_(0.0),
      resize_width_(resize_width)
{
    quality_ = (quality <= 0 || quality > 100) ? 80 : quality;
    resize_width_ = (resize_width_ <= 0.0) ? 640.0 : resize_width_;

    cv::setNumThreads(2);
    cv::setUseOptimized(true);
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
    // 1. 基本的なnullチェック
    if (frame.data == nullptr || frame.length == 0) {
        LOG_E("Invalid input frame data");

        return false;
    }

    cv::Mat img;
    uint8_t* data_ptr = static_cast<uint8_t*>(frame.data);

    // MJPEG判定
    bool is_mjpeg = (frame.length > 2 && data_ptr[0] == 0xFF && data_ptr[1] == 0xD8);

    if (is_mjpeg) {
        // --- MJPEG ---
        cv::Mat raw_wrapper(1, frame.length, CV_8UC1, frame.data);
        try {
            img = cv::imdecode(raw_wrapper, cv::IMREAD_COLOR);
        } catch (const cv::Exception& ex) {
            LOG_E("MJPEG decode failed: %s", ex.what());

            return false;
        }
    } else {
        // --- YUYV (Raw) ---
        
        // 幅と高さが0の場合はデフォルト値を入れる
        int w = (frame.width > 0) ? frame.width : 640;
        int h = (frame.height > 0) ? frame.height : 480;

        // 【ここが重要！】
        // バッファサイズが足りているか確認する
        // YUYVは 1ピクセル2バイトなので、必要なサイズは w * h * 2
        size_t expected_size = static_cast<size_t>(w * h * 2);
        
        if (frame.length < expected_size) {
            // サイズが足りない場合、無理に変換しようとするとSegfaultになるので中止する
            LOG_E("Buffer too small for YUYV %dx%d! Expected: %zu, Got: %zu", 
                  w, h, expected_size, frame.length);
            return false;
        }

        try {
            // CV_8UC2 (2チャンネル) として読み込む
            cv::Mat yuyv_mat(h, w, CV_8UC2, frame.data);
            cv::cvtColor(yuyv_mat, img, cv::COLOR_YUV2BGR_YUYV);
        } catch (const cv::Exception& ex) {
            LOG_E("YUYV conversion failed: %s", ex.what());
            return false;
        }
    }

    if (img.empty()) {
        LOG_E("Decoded image is empty");
        return false;
    }

    // 例外処理を追加して保護する
    try {
        // 2. ノイズ除去
        //cv::GaussianBlur(img, img, cv::Size(5, 5), 0);

        // 3. コントラスト補正
        img.convertTo(img, -1, contrast_alpha_, brightness_beta_);
        
        // 生データを保存
        //output_data.raw_mat = img.clone();
        output_data.raw_mat = cv::Mat();

        cv::Mat send_img;

        if (img.cols > resize_width_) {
            double scale = resize_width_ / img.cols;

            cv::resize(img, send_img, cv::Size(), scale, scale);
        }
        else {
            send_img = img;
        }

        // 4. 送信用JPEGエンコード
        if (!encode_image_to_jpeg(img, output_data.send_encoded_image, 80)) {
            LOG_E("Failed to encode standard image");

            return false;
        }
    } catch (const cv::Exception& ex) {
        LOG_E("Image processing exception: %s", ex.what());

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
    if (!encode_image_to_jpeg(output_data.std_processed_mat, output_data.send_encoder_image, quality_
    )) {
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
