/**
 * @file    image_processor.cpp
 * @brief   画像処理クラスの実装
 * @author  sawada souta
 * @date    2025-12-14
 */

#include <vector>

#include <turbojpeg.h>

#include "image_processor/image_processor.hpp"
#include "logger/logger.hpp"

// コンストラクタ
ImageProcessor::ImageProcessor(uint8_t quality, double resize_width)
    : quality_(quality),
      contrast_alpha_(1.0),
      brightness_beta_(0.0),
      resize_width_(resize_width),
      tj_dec_(nullptr),
      bgr_buffer_(),
      tj_enc_(nullptr),
      jpg_buffer_()
{
    quality_ = (quality <= 0 || quality > 100) ? 80 : quality;
    resize_width_ = (resize_width_ <= 0.0) ? 640.0 : resize_width_;

    cv::setNumThreads(2);
    cv::setUseOptimized(true);

    tj_dec_ = tjInitDecompress();
    if (tj_dec_ == nullptr) {
        LOG_E("Failed to initialize TurboJPEG decompressor: %s", tjGetErrorStr());

        exit(-1);
    }

    tj_enc_ = tjInitCompress();
    if (tj_enc_ == nullptr) {
        LOG_E("Failed to initialize TurboJPEG compressor: %s", tjGetErrorStr());

        exit(-1);
    }
}

// デストラクタ
ImageProcessor::~ImageProcessor()
{
    if (tj_dec_ != nullptr) {
        tjDestroy(tj_dec_);
    }

    if (tj_enc_ != nullptr) {
        tjDestroy(tj_enc_);
    }
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

    uint8_t* data_ptr = static_cast<uint8_t*>(frame.data);

    // MJPEG判定 (SOIマーカー: 0xFF, 0xD8)
    bool is_mjpeg = (frame.length > 2 && data_ptr[0] == 0xFF && data_ptr[1] == 0xD8);

    if (is_mjpeg) {
        // --- 【修正】 パディング除去ロジック ---
        // V4L2バッファは末尾にゴミ(0x00等)を含むことがあるため、
        // 後ろから 0xFF 0xD9 (EOI) を探して、正しいサイズを特定する。
        
        size_t actual_length = 0;
        bool eoi_found = false;

        // 最大で末尾1024バイトまで遡ってEOIを探す
        size_t scan_limit = (frame.length > 1024) ? (frame.length - 1024) : 0;

        for (size_t i = frame.length - 1; i > scan_limit; --i) {
            if (data_ptr[i] == 0xD9 && data_ptr[i-1] == 0xFF) {
                actual_length = i + 1; // 0xD9の後ろまでを含める
                eoi_found = true;
                break;
            }
        }

        if (!eoi_found) {
            // EOIが見つからない場合は不完全なフレームとして捨てる
            // LOG_W("Incomplete MJPEG frame (No EOI marker found). Skipping.");
            return false;
        }
        // ----------------------------------------

        // 抽出した正しいサイズでコピー
        output_data.send_encoded_image.assign(data_ptr, data_ptr + actual_length);
        output_data.is_mjpeg_passthrough = true;
        
        int w, h, subsamp, colorspace; 
        
        // actual_length を渡すことが重要
        if (tjDecompressHeader3(
                tj_dec_,
                data_ptr,
                actual_length, 
                &w,
                &h,
                &subsamp,
                &colorspace) != 0) {
            // LOG_E("TurboJPEG decompress header failed: %s", tjGetErrorStr());
            return false;
        }

        size_t required_size = static_cast<size_t>(w * h * 3);
        if (bgr_buffer_.size() < required_size) {
            bgr_buffer_.resize(required_size);
        }

        if (tjDecompress2(
                tj_dec_,
                data_ptr,
                actual_length, // ここも修正
                bgr_buffer_.data(),
                w,
                0,
                h,
                TJPF_BGR,
                TJFLAG_FASTDCT) != 0) {
            // LOG_E("TurboJPEG decompress failed: %s", tjGetErrorStr());
            return false;
        }

        output_data.raw_mat = cv::Mat(h, w, CV_8UC3, bgr_buffer_.data());

        return true;
    } else {
        // --- YUYV (Raw) ---
        int w = (frame.width > 0) ? frame.width : 640;
        int h = (frame.height > 0) ? frame.height : 480;

        size_t expected_size = static_cast<size_t>(w * h * 2);
        
        if (frame.length < expected_size) {
            LOG_E("Buffer too small for YUYV %dx%d! Expected: %zu, Got: %zu", 
                  w, h, expected_size, frame.length);
            return false;
        }

        try {
            cv::Mat yuyv_mat(h, w, CV_8UC2, frame.data);
            cv::cvtColor(yuyv_mat, output_data.raw_mat, cv::COLOR_YUV2BGR_YUYV);
        } catch (const cv::Exception& ex) {
            LOG_E("YUYV conversion failed: %s", ex.what());
            return false;
        }

        output_data.is_mjpeg_passthrough = false;

        return encode_image_to_jpeg(
            output_data.raw_mat,
            output_data.send_encoded_image,
            quality_);
    }
}

// AI解析用処理パイプライン
bool ImageProcessor::process_for_ai(const cv::Mat& src_img, AnalysisProcessedData& output_data)
{
    if (src_img.empty()) {
        LOG_E("Input image for AI processing is empty");
        return false;
    }

    try {
        // =========================
        // 1. ノイズ除去（色保持）
        // =========================
        // GaussianBlur は色境界を壊しやすいので弱め or bilateral
        cv::Mat denoised;
        cv::bilateralFilter(
            src_img,
            denoised,
            5,      // 近傍サイズ
            50,     // 色差
            50      // 空間距離
        );

        // =========================
        // 2. HSV変換（色解析用）
        // =========================
        cv::Mat hsv;
        cv::cvtColor(denoised, hsv, cv::COLOR_BGR2HSV);

        // =========================
        // 3. エッジ検出（形状用）
        // =========================
        cv::Mat gray, edges;
        cv::cvtColor(denoised, gray, cv::COLOR_BGR2GRAY);

        // Canny は2値化だが「エッジ専用」なのでOK
        cv::Canny(gray, edges, 50, 150);

        // =========================
        // 4. 輪郭抽出（抵抗候補）
        // =========================
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(
            edges,
            contours,
            cv::RETR_EXTERNAL,
            cv::CHAIN_APPROX_SIMPLE
        );

        // =========================
        // 5. デバッグ用可視化
        // =========================
        output_data.std_processed_mat = src_img.clone();

        for (const auto& c : contours) {
            // 小さすぎるノイズ除去
            if (cv::contourArea(c) < 300.0)
                continue;

            // 抵抗は細長い → 外接矩形で判定（今後精密化）
            cv::RotatedRect r = cv::minAreaRect(c);
            float w = r.size.width;
            float h = r.size.height;
            float aspect = std::max(w, h) / std::min(w, h);

            if (aspect < 3.0)  // 細長くないものを除外
                continue;

            cv::Point2f pts[4];
            r.points(pts);
            for (int i = 0; i < 4; i++) {
                cv::line(
                    output_data.std_processed_mat,
                    pts[i],
                    pts[(i + 1) % 4],
                    cv::Scalar(0, 255, 0),
                    2
                );
            }

            // 将来：
            // - 向き = r.angle
            // - ROI = boundingRect
            // - HSV分布でカラーコード判別
        }

        // =========================
        // 6. AI結果送信用JPEG
        // =========================
        if (!encode_image_to_jpeg(
                output_data.std_processed_mat,
                output_data.send_encoder_image,
                quality_)) {
            LOG_E("Failed to encode analysis image");
            return false;
        }

    } catch (const cv::Exception& ex) {
        LOG_E("AI processing exception: %s", ex.what());
        return false;
    }

    return true;
}

bool ImageProcessor::encode_image_to_jpeg(
    const cv::Mat& img,
    std::vector<uint8_t>& encoded_buffer,
    uint8_t quality)
{
    if (img.empty()) return false;

    unsigned char* jpeg_ptr = nullptr;
    unsigned long jpeg_size = 0;

    int ret = tjCompress2(
        tj_enc_,
        img.data,
        img.cols,
        img.step,
        img.rows,
        TJPF_BGR,
        &jpeg_ptr,
        &jpeg_size,
        TJSAMP_420,
        quality,
        TJFLAG_FASTDCT
    );

    if (ret != 0) {
        LOG_E("TurboJPEG compress failed: %s", tjGetErrorStr());
        return false;
    }

    encoded_buffer.assign(jpeg_ptr, jpeg_ptr + jpeg_size);
    tjFree(jpeg_ptr);

    return true;
}
